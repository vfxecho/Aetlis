#include "Player.h"
#include "../worlds/World.h"
#include "../ServerHandle.h"
#include "../bots/PlayerBot.h"
#include "../protocols/ProtocolVanis.h"
#include "../sockets/Router.h"
#include "../sockets/DualMinionRouter.h"

using std::to_string;

Player::Player(ServerHandle* handle, unsigned int id, Router* router) :
	handle(handle), id(id), router(router) {
	viewArea.w /= handle->runtime.playerViewScaleMult;
	viewArea.h /= handle->runtime.playerViewScaleMult;
};

Player::~Player() {
	// Logger::debug(string("Deallocating player: ") + leaderboardName);
	if (hasWorld || world)
		Logger::warn(string("Player: ") + leaderboardName + " should NOT have reference to world while being deallocated");

	if (router->disconnected) {
		router->hasPlayer = false;
		router->player = nullptr;
		if (router->type == RouterType::PLAYER)
			delete (Connection*) router;
		else if (router->type == RouterType::PLAYER_BOT)
			delete (PlayerBot *) router;
		router = nullptr;
	}
};

void Player::updateState(PlayerState targetState) {
	if (!world) state = PlayerState::DEAD;
	else if (ownedCells.size()) {
		state = PlayerState::ALIVE;
		if (router->spectateTarget) router->spectateTarget->spectators.remove(router);
		router->spectateTarget = nullptr;
	} else if (targetState == PlayerState::DEAD) {
		state = PlayerState::DEAD;
		justDied = true;
		router->onDead();
		killCount = 0;
		maxScore = 0;
	} else if (!world->largestPlayer) state = PlayerState::ROAM;
	else if (state == PlayerState::SPEC && targetState == PlayerState::ROAM) state = PlayerState::ROAM;
	else state = PlayerState::SPEC;
};

void Player::updateViewArea() {

	if (!world) return;

	float size = 0, size_x = 0, size_y = 0, x = 0, y = 0, current_score = 0, factor = 0;
	float min_x = world->border.getX() + world->border.w;
	float max_x = world->border.getX() - world->border.w;
	float min_y = world->border.getY() + world->border.h;
	float max_y = world->border.getY() - world->border.h;

	Player* viewFocusPlayer = this;
	if (m_playerType == PlayerType::DUAL_MINION && m_ownerPlayer) {
		viewFocusPlayer = m_ownerPlayer;
	} else if (m_playerType == PlayerType::REGULAR && m_isDualActive && m_dualPlayer) {
		viewFocusPlayer = this;
	}

	switch (viewFocusPlayer->state) {
		case PlayerState::DEAD:
			this->score = -1;
			if (m_dualPlayer) m_dualPlayer->score = -1;
			break;
		case PlayerState::ALIVE: {
			std::list<PlayerCell*> cellsToConsider;
			Player* owner_ptr = nullptr;
			Player* dual_ptr = nullptr;

			if (viewFocusPlayer->m_playerType == PlayerType::REGULAR) {
				owner_ptr = viewFocusPlayer;
				dual_ptr = viewFocusPlayer->m_dualPlayer;
			} else if (viewFocusPlayer->m_playerType == PlayerType::DUAL_MINION && viewFocusPlayer->m_ownerPlayer) {
				owner_ptr = viewFocusPlayer->m_ownerPlayer;
				dual_ptr = viewFocusPlayer;
			}

			if (owner_ptr) {
				for (auto cell : owner_ptr->ownedCells) {
					cellsToConsider.push_back(cell);
				}
			}
			if (dual_ptr) {
				for (auto cell : dual_ptr->ownedCells) {
					cellsToConsider.push_back(cell);
				}
			}

			if (cellsToConsider.empty()) {
				this->score = 0;
				if(owner_ptr && owner_ptr != this) owner_ptr->score = 0;
				if(dual_ptr && dual_ptr != this) dual_ptr->score = 0;
				viewArea.setX(viewFocusPlayer->viewArea.getX());
				viewArea.setY(viewFocusPlayer->viewArea.getY());
				viewArea.w = 4000.0f * handle->runtime.playerViewScaleMult;
				viewArea.h = 4000.0f * handle->runtime.playerViewScaleMult;
				viewArea.s = 4000.0f;
				break;
			}

			for (auto cell : cellsToConsider) {
				x += cell->getX() * cell->getSize();
				y += cell->getY() * cell->getSize();
				min_x = std::min(min_x, cell->getX());
				max_x = std::max(max_x, cell->getX());
				min_y = std::min(min_y, cell->getY());
				max_y = std::max(max_y, cell->getY());
				current_score += cell->getMass();
				size += cell->getSize();
			}

			if (owner_ptr) owner_ptr->score = 0;
			if (dual_ptr) dual_ptr->score = 0;
			float owner_individual_score = 0;
			float dual_individual_score = 0;

			if (owner_ptr) {
			    for (auto cell : owner_ptr->ownedCells) owner_individual_score += cell->getMass();
			    owner_ptr->score = owner_individual_score;
			    owner_ptr->maxScore = std::max(owner_individual_score, owner_ptr->maxScore);
			}
			if (dual_ptr) {
			    for (auto cell : dual_ptr->ownedCells) dual_individual_score += cell->getMass();
			    dual_ptr->score = dual_individual_score;
			    dual_ptr->maxScore = std::max(dual_individual_score, dual_ptr->maxScore);
			}
            
            if (this->m_playerType == PlayerType::REGULAR) {
                if (this->m_dualPlayer) {
                    this->score = owner_individual_score + dual_individual_score;
                } else {
                    this->score = owner_individual_score;
                }
            } else if (this->m_playerType == PlayerType::DUAL_MINION) {
                this->score = dual_individual_score;
            } else {
                 this->score = 0;
            }
            
            current_score = owner_individual_score + dual_individual_score;

			float scoreForRestartCheck = current_score;
			if ((scoreForRestartCheck > world->border.w * world->border.h / 100.0f * handle->runtime.restartMulti) && handle->tick > 500) {
				std::string killerName = owner_ptr ? owner_ptr->leaderboardName : (dual_ptr ? dual_ptr->leaderboardName : "A large player");
				if (handle->runtime.killOversize) {
					if (owner_ptr) world->killPlayer(owner_ptr, true);
					if (dual_ptr && dual_ptr != owner_ptr) world->killPlayer(dual_ptr, true);
					world->worldChat->broadcast(nullptr, string(killerName) + " died from extreme obesity (" + to_string((int)(scoreForRestartCheck / 1000.0f)) + "k mass)");
				} else {
					world->shouldRestart = true;
					world->worldChat->broadcast(nullptr, string(killerName) + " destroyed the server with " + to_string((int)(scoreForRestartCheck / 1000.0f)) + "k mass");
				}
			}

			factor = pow(cellsToConsider.size() + 50, 0.1);
			viewArea.setX(x / size);
			viewArea.setY(y / size);
			float view_size_score_formula = current_score;
			size = (factor + 1) * sqrt(view_size_score_formula * 100.0);
			size_x = size_y = std::max(size, 4000.0f);
			size_x = std::max(size_x, (viewArea.getX() - min_x) * 1.75f);
			size_x = std::max(size_x, (max_x - viewArea.getX()) * 1.75f);
			size_y = std::max(size_y, (viewArea.getY() - min_y) * 1.75f);
			size_y = std::max(size_y, (max_y - viewArea.getY()) * 1.75f);
			viewArea.w = size_x * handle->runtime.playerViewScaleMult;
			viewArea.h = size_y * handle->runtime.playerViewScaleMult;
			viewArea.s = size;
			break;
		}
		case PlayerState::SPEC:
			this->score = -1;
			if (m_dualPlayer) m_dualPlayer->score = -1;
			if (!router->spectateTarget || !router->spectateTarget->player) break;
			viewArea = router->spectateTarget->player->viewArea;
			break;
		case PlayerState::ROAM:
			this->score = -1;
			if (m_dualPlayer) m_dualPlayer->score = -1;
			float dx = router->mouseX - viewArea.getX();
			float dy = router->mouseY - viewArea.getY();
			float d = sqrt(dx * dx + dy * dy);
			float D = std::min(d, handle->runtime.playerRoamSpeed);
			if (D < 1) break;
			dx /= d; dy /= d;
			auto b = &world->border;
			viewArea.setX(std::max(b->getX() - b->w, std::min(viewArea.getX() + dx * D, b->getX() + b->w)));
			viewArea.setY(std::max(b->getY() - b->h, std::min(viewArea.getY() + dy * D, b->getY() + b->h)));
			size = viewArea.s = handle->runtime.playerRoamSpeed;
			viewArea.w = 1920 / size / 2 * handle->runtime.playerViewScaleMult;
			viewArea.h = 1080 / size / 2 * handle->runtime.playerViewScaleMult;
			break;
	}
}

void Player::updateVisibleCells(bool threaded) {
	if (!hasWorld || !world) return;

	// Determine the player group (owner and dual if applicable)
	Player* owner_player = nullptr;
	Player* dual_player_ptr = nullptr;

	if (m_playerType == PlayerType::REGULAR) {
		owner_player = this;
		dual_player_ptr = this->m_dualPlayer;
	} else if (m_playerType == PlayerType::DUAL_MINION && m_ownerPlayer) {
		owner_player = this->m_ownerPlayer;
		dual_player_ptr = this;
	}

	if (threaded && lockedFinder) {
		lastVisibleCellData.clear();
		lastVisibleCellData = visibleCellData;
		visibleCellData.clear();

		// Add owned cells from owner
		if (owner_player) {
			for (auto data : owner_player->ownedCellData) // Assuming owner_player is 'this' or correctly points to actual owner
				if (data->type != CellType::EJECTED_CELL || data->age > 1)
					visibleCellData.insert(std::make_pair(data->id, data));
		}
		// Add owned cells from dual
		if (dual_player_ptr) {
			for (auto data : dual_player_ptr->ownedCellData)
				if (data->type != CellType::EJECTED_CELL || data->age > 1)
					visibleCellData.insert(std::make_pair(data->id, data));
		}

		lockedFinder->search(viewArea, [this](auto c) {
			auto data = (CellData*)c;
			if (data->type != CellType::EJECTED_CELL || data->age > 1)
				visibleCellData.insert(std::make_pair(data->id, data));
			return false;
		});

	}
	else {
		lastVisibleCells.clear();
		lastVisibleCells = visibleCells;
		visibleCells.clear();

		// Add owned cells from owner
		if (owner_player) {
			for (auto cell : owner_player->ownedCells) // Assuming owner_player is 'this' or correctly points to actual owner
				if (!cell->inside && (cell->getType() != CellType::EJECTED_CELL || cell->getAge() > 1))
					visibleCells.insert(std::make_pair(cell->id, cell));
		}
		// Add owned cells from dual
		if (dual_player_ptr) {
			for (auto cell : dual_player_ptr->ownedCells)
				if (!cell->inside && (cell->getType() != CellType::EJECTED_CELL || cell->getAge() > 1))
					visibleCells.insert(std::make_pair(cell->id, cell));
		}

		world->finder->search(viewArea, [this](auto c) {
			auto cell = (Cell*)c;
			if (cell->getType() != CellType::EJECTED_CELL || cell->getAge() > 1)
				visibleCells.insert(std::make_pair(cell->id, cell));
			return false;
		});

		/*
		printf("Visible Cells: ");
		for (auto [id, _] : visibleCells) {
			printf("%u, ", id);
		}
		printf("\n");

		printf("Lastvisible Cells: ");
		for (auto [id, _] : lastVisibleCells) {
			printf("%u, ", id);
		}
		printf("\n"); */
	}
}

bool Player::exist() {
	if (!router->disconnected) return true;
	world->killPlayer(this);
	world->removePlayer(this);
	handle->removePlayer(this->id);
	return false;
}

void Player::createDualPlayer() {
	if (m_playerType == PlayerType::DUAL_MINION) {
		Logger::warn("[Player " + to_string(id) + "] DUAL_MINION cannot create a dual player.");
		return;
	}
	if (m_dualPlayer) {
		Logger::warn("[Player " + to_string(id) + "] Attempted to create dual player, but one already exists.");
		if (this->router && this->router->type == RouterType::PLAYER) {
			Connection* conn = static_cast<Connection*>(this->router);
			if (conn->protocol) {
				ProtocolVanis* vanisProtocol = dynamic_cast<ProtocolVanis*>(conn->protocol);
				if (vanisProtocol) {
					vanisProtocol->sendDualPlayerUpdate(this);
				} else {
					Logger::warn("Player " + std::to_string(this->id) + ": Router protocol is not ProtocolVanis in createDualPlayer (already exists) context.");
				}
			}
		}
		return;
	}

	Logger::info("[Player " + to_string(id) + "] Creating dual player.");

	DualMinionRouter* botRouter = new DualMinionRouter(&(handle->listener), this->world);
	if (!botRouter) {
		Logger::error("[Player " + to_string(id) + "] Failed to allocate DualMinionRouter.");
		return;
	}
	
	m_dualPlayer = handle->createPlayer(botRouter);
	if (!m_dualPlayer) {
		Logger::error("[Player " + to_string(id) + "] Failed to create player instance for dual minion.");
		delete botRouter; // Clean up allocated router
		return;
	}

	m_dualPlayer->m_playerType = PlayerType::DUAL_MINION;
	m_dualPlayer->m_ownerPlayer = this;
	botRouter->player = m_dualPlayer; // Assign the player to the botRouter

	// Set up dual player's initial state (name, color, etc.) - can be same as owner or distinct
	m_dualPlayer->cellName = this->cellName + " (Dual)"; // Example name
	m_dualPlayer->leaderboardName = ""; // Dual minions usually don't appear on leaderboard directly
	m_dualPlayer->chatName = this->chatName + " (Dual)";
	m_dualPlayer->cellColor = this->cellColor; // Or a different color
	m_dualPlayer->cellSkin = this->cellSkin;

	// Don't spawn cells yet, that happens on first activation via opcode 23
	// m_dualPlayer->updateState(PlayerState::ALIVE); // Not yet, no cells

	Logger::info("[Player " + to_string(id) + "] Dual player " + to_string(m_dualPlayer->id) + " created successfully.");

	// Send update to client
	if (this->router && this->router->type == RouterType::PLAYER) {
		Connection* conn = static_cast<Connection*>(this->router);
		if (conn->protocol) {
			ProtocolVanis* vanisProtocol = dynamic_cast<ProtocolVanis*>(conn->protocol);
			if (vanisProtocol) {
				vanisProtocol->sendDualPlayerUpdate(this);
			} else {
				Logger::warn("Player " + std::to_string(this->id) + ": Router protocol is not ProtocolVanis in createDualPlayer (success) context.");
			}
		}
	}
}

void Player::setDualActive(bool active) {
	if (m_playerType == PlayerType::DUAL_MINION) {
		Logger::warn("[Player " + to_string(id) + "] DUAL_MINION cannot set dual active state. Control is with owner: " + (m_ownerPlayer ? to_string(m_ownerPlayer->id) : "null"));
		return;
	}
	if (!m_dualPlayer) {
		Logger::warn("[Player " + to_string(id) + "] Attempted to set dual active, but no dual player exists.");
		m_isDualActive = false; // Ensure it's false
	} else {
		m_isDualActive = active;
		Logger::info("[Player " + to_string(id) + "] Dual active state set to: " + (m_isDualActive ? "true" : "false") + " for dual player " + to_string(m_dualPlayer->id));
	}

	// Notify the client about the change in active player
	if (this->router && this->router->type == RouterType::PLAYER) {
		Connection* conn = static_cast<Connection*>(this->router);
		if (conn->protocol) {
			ProtocolVanis* vanisProtocol = dynamic_cast<ProtocolVanis*>(conn->protocol);
			if (vanisProtocol) {
				vanisProtocol->sendDualPlayerUpdate(this);
			} else {
				Logger::warn("Player " + std::to_string(this->id) + ": Router protocol is not ProtocolVanis in setDualActive context.");
			}
		}
	}
}

Player* Player::getActiveControlledPlayer() {
	if (m_playerType == PlayerType::DUAL_MINION) {
		if (m_ownerPlayer) {
			return m_ownerPlayer->getActiveControlledPlayer();
		}
		Logger::warn("[Player " + to_string(id) + "] DUAL_MINION has no owner, returning self as active (unexpected).");
		return this; 
	}

	if (m_dualPlayer && m_isDualActive) {
		return m_dualPlayer;
	}
	return this;
}

bool Player::isDualMode() const {
	if (m_playerType == PlayerType::REGULAR) {
		return m_dualPlayer != nullptr;
	}
	if (m_playerType == PlayerType::DUAL_MINION && m_ownerPlayer) {
		return m_ownerPlayer->isDualMode();
	}
	return false;
}

bool Player::isOwnedByDualMaster() const {
	return m_playerType == PlayerType::DUAL_MINION && m_ownerPlayer != nullptr;
}