#include "World.h"
#include "../sockets/Router.h"
#include "../ServerHandle.h"
#include "../cells/Cell.h"
#include "../bots/PlayerBot.h"
#include <random>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <algorithm>
#include <utility>
#include <atomic>
#include <chrono>
#include <cmath>
#include <unordered_map>

using std::to_string;
using std::thread;

World::World(ServerHandle* handle, unsigned int id) : handle(handle), id(id) {
	worldChat = new ChatChannel(&handle->listener);
	physicsPool = new ThreadPool(handle->getSettingInt("physicsThreads"));

	Logger::info(string("Using ") + std::to_string(handle->getSettingInt("physicsThreads")) + " threads to accelerate physics");
	Logger::info(string("Using ") + std::to_string(handle->getSettingInt("socketsThreads")) + " threads to accelerate sockets");

	float x = handle->getSettingInt("worldMapX");
	float y = handle->getSettingInt("worldMapY");
	float w = handle->getSettingInt("worldMapW");
	float h = handle->getSettingInt("worldMapH");
	Rect rect(x, y, w, h);
	setBorder(rect);
	handle->bench = true;
}

void World::afterCreation() {
	int bots = handle->getSettingInt("worldPlayerBotsPerWorld");
	if (!bots) return;
	unsigned int worldId = this->id;
	ServerHandle* svHandle = this->handle;

	while (bots-- > 0) {
		unsigned int delay = bots * 10;
		svHandle->ticker.timeout(delay, [svHandle, worldId] {
			if (svHandle->worlds.count(worldId)) {
				World* world = svHandle->worlds[worldId];
				auto bot = new PlayerBot(world);
				bot->createPlayer();

				if (svHandle->worlds.count(worldId)) {
					world->addPlayer(bot->player);
				} else {
					delete bot;
				}
			}
		});
	}
}

World::~World() {
	Logger::debug(string("Deallocating world (id: ") + std::to_string(id) + ")");
	for (auto player : players) {
		// Should not delete player since ServerHandle handles it
		player->hasWorld = false;
		player->world = nullptr;
	}
	players.clear();
	for (auto c : cells) delete c;
	cells.clear();
	for (auto c : gcTruck) delete c;
	gcTruck.clear();
	delete worldChat;
	delete finder;
	delete physicsPool;
}

void World::setBorder(Rect& rect) {
	border = rect;
	if (finder) delete finder;
	int maxLevel = handle->getSettingInt("worldFinderMaxLevel");
	int maxItems = handle->getSettingInt("worldFinderMaxItems");
	int maxSearch = handle->getSettingInt("worldFinderMaxSearch");
	finder = new QuadTree(border, maxLevel, maxItems);
	finder->maxSearch = maxSearch;
	for (auto cell : cells) {
		if (cell->getType() == PLAYER) continue;
		finder->insert(cell);
		if (!border.fullyIntersects(cell->range))
			removeCell(cell);
	}
}

void World::addCell(Cell* cell) {
	cell->exist = true;
	cell->range = { cell->getX(), cell->getY(), cell->getSize(), cell->getSize() };
	cells.push_back(cell);
	finder->insert(cell);
	cell->onSpawned();
	handle->gamemode->onNewCell(cell);
}

void World::restart() {
	if (!shouldRestart) return;

	Logger::info(string("World (id: ") + to_string(id) + ") restarting!");
	for (auto c : cells) delete c;
	cells.clear();
	for (auto c : gcTruck) delete c;
	gcTruck.clear();
	for (auto p : players) {
		p->lastVisibleCellData.clear();
		p->lastVisibleCells.clear();
		p->visibleCellData.clear();
		p->visibleCells.clear();
		p->ownedCellData.clear();
		p->ownedCells.clear();
		p->updateState(PlayerState::DEAD);
	}

	virusCount = 0;
	pelletCount = 0;
	motherCellCount = 0;

	handle->gamemode->compileLeaderboard(this);
	setBorder(border);
	_nextCellId = 1;
	worldChat->broadcast(nullptr, "Server restarted");
	shouldRestart = false;
}

void World::updateCell(Cell* cell) {
	cell->range = {
		cell->getX(),
		cell->getY(),
		cell->getSize(),
		cell->getSize()
	};
	finder->update(cell);
}

void World::removeCell(Cell* cell) {
	if (!cell->exist) return;
	cell->exist = false;
	cell->deadTick = handle->tick;
	gcTruck.push_back(cell);
	handle->gamemode->onCellRemove(cell);
	cell->onRemoved();
	finder->remove(cell);
}

void World::clearTruck() {
	auto iter = gcTruck.begin();
	while (iter != gcTruck.end()) {
		auto cell = *iter;
		if (handle->tick - cell->deadTick > 100) {
			delete cell;
			iter = gcTruck.erase(iter);
		} else break;
	}
}

void World::addPlayer(Player* player) {

	players.push_back(player);
	player->world = this;
	player->hasWorld = true;

	if (player->router->type == RouterType::PLAYER)
		worldChat->add((Connection*)(player->router));

	handle->gamemode->onPlayerJoinWorld(player, this);
	player->router->onWorldSet();

	if (player->router->type == RouterType::PLAYER)
		Logger::debug(string("Player ") + to_string(player->id) + string(" has been added to world ") + to_string(id));
	if (!player->router->isExternal()) return;
	int minionsPerPlayer = handle->getSettingInt("worldMinionsPerPlayer");
	while (minionsPerPlayer-- > 0) {
		// TODO: add minions
	}
}

void World::removePlayer(Player* player) {
	handle->gamemode->onPlayerLeaveWorld(player, this);
	player->world = nullptr;
	player->hasWorld = false;

	if (player->router->type == RouterType::PLAYER)
		worldChat->remove((Connection*) player->router);

	while (player->ownedCells.size())
		removeCell(player->ownedCells.front());
	player->router->onWorldReset();
	Logger::debug(string("player ") + to_string(player->id) + " has been removed from world " + to_string(id));
};

void World::killPlayer(Player* player, bool instantKill) {

	if (player->state != PlayerState::ALIVE) return;

	for (auto c : player->ownedCells) {
		if (instantKill) {
			removeCell(c);
		} else {
			c->owner = nullptr;
			c->posChanged = true;
			c->id = getNextCellId();
			if (c->data) c->data->dead = true;
		}
	}

	player->ownedCells.clear();
	player->lastVisibleCells.clear();
	player->visibleCells.clear();
	player->lastVisibleCellData.clear();
	player->visibleCellData.clear();

	for (auto c : player->ownedCellData) delete c;
	player->ownedCellData.clear();
}

Point World::getRandomPos(float cellSize) {
	return {
		border.getX() - border.w + cellSize + (float) randomZeroToOne * (2 * border.w - cellSize),
		border.getY() - border.h + cellSize + (float) randomZeroToOne * (2 * border.h - cellSize)
	};
}

bool World::isSafeSpawnPos(Rect& range) {
	return !finder->containAny(range, [](auto item) { return ((Cell*) item)->shouldAvoidWhenSpawning(); });
}

Point World::getSafeSpawnPos(float& cellSize, bool& failed) {
	int tries = handle->runtime.worldSafeSpawnTries;
	cellSize *= 1.2f;
	while (--tries >= 0) {
		auto pos = getRandomPos(cellSize);
		Rect rect(pos.getX(), pos.getY(), cellSize, cellSize);
		if (isSafeSpawnPos(rect)) {
			cellSize /= 1.2f;
			return Point(pos);
		}
		cellSize *= 0.998;
	}
	failed = true;
	return Point(getRandomPos(cellSize));
}

SpawnResult World::getPlayerSpawn(float& cellSize, bool& failed) {
	double rnd = randomZeroToOne;
	float chance = handle->runtime.worldSafeSpawnFromEjectedChance;
	return { 0, getSafeSpawnPos(cellSize, failed) };
}

SpawnResult World::getPlayerSpawnNearPoint(Point& deathPoint, float& cellSize, bool& failed) {
	// --- BEGIN Log ---
	Logger::debug("[SpawnNearPoint] Player (cellsize: " + std::to_string(cellSize) + ") -> Attempting spawn near (" + std::to_string(deathPoint.getX()) + ", " + std::to_string(deathPoint.getY()) + ")");
	// --- END Log ---
	int tries = handle->runtime.worldSafeSpawnTries; // Reuse the same number of tries
	float radius = handle->runtime.worldRespawnNearDeathRadius;
	failed = true; // Assume failure initially

	// Try to find a spot near the death point
	float tempCellSize = cellSize * 1.2f;
	for (int i = 0; i < tries; ++i) {
		// Generate a random point within the square (deathPoint.x +/- radius, deathPoint.y +/- radius)
		float offsetX = static_cast<float>(randomZeroToOne) * 2.0f * radius - radius;
		float offsetY = static_cast<float>(randomZeroToOne) * 2.0f * radius - radius;
		Point potentialPos = { deathPoint.getX() + offsetX, deathPoint.getY() + offsetY };

		// Clamp to world boundaries - ensure all operands for min/max are float
		float minX = border.getX() - border.w + tempCellSize / 2.0f;
		float maxX = border.getX() + border.w - tempCellSize / 2.0f;
		float minY = border.getY() - border.h + tempCellSize / 2.0f;
		float maxY = border.getY() + border.h - tempCellSize / 2.0f;

		potentialPos.setX(std::max(minX, std::min(potentialPos.getX(), maxX)));
		potentialPos.setY(std::max(minY, std::min(potentialPos.getY(), maxY)));

		Rect spawnRect(potentialPos.getX(), potentialPos.getY(), tempCellSize, tempCellSize);
		if (isSafeSpawnPos(spawnRect)) {
			cellSize = tempCellSize / 1.2f;
			failed = false;
			return { 0, potentialPos }; // Found a safe spot
		}
		tempCellSize *= 0.998f;
	}

	// If still failed to find a point *near death*, fall back to the regular safe spawn.
	Logger::debug("[SpawnNearPoint] Failed to find spawn near death point for Player (cellsize: " + std::to_string(cellSize) + ") at (" + std::to_string(deathPoint.getX()) + ", " + std::to_string(deathPoint.getY()) + "). Falling back to random spawn.");
	// The 'failed' parameter will be set by the getPlayerSpawn call.
	// The SpawnResult from getPlayerSpawn becomes the result of this function.
	return getPlayerSpawn(cellSize, failed); 
}

void World::spawnPlayer(Player* player, Point& pos, float size) {

	// Reset linelock state on spawn/respawn
	player->isLineLocked = false;
	player->lineEqA = 0.0f;
	player->lineEqB = 0.0f;
	player->lineEqC = 0.0f;
	player->lineEqDenomInv = 0.0f;
	// --- BEGIN Log ---
	Logger::debug("[World::spawnPlayer] Reset linelock state for player " + std::to_string(player->id));
	// --- END Log ---

	if (player->router->type == RouterType::PLAYER)
		((Connection*)player->router)->protocol->onPlayerSpawned(player);
		
	for (auto other : players) {
		if (other == player) continue;
		if (player->router->type == RouterType::PLAYER)
			((Connection*)player->router)->protocol->onPlayerSpawned(other);
		if (other->router->type == RouterType::PLAYER)
			((Connection*)other->router)->protocol->onPlayerSpawned(player);
	}

	auto playerCell = new PlayerCell(this, player, pos.getX(), pos.getY(), size);
	addCell(playerCell);
	player->joinTick = handle->tick;
	player->updateState(PlayerState::ALIVE);

	// Start spawn cooldown
	int delayMillis = 1000; // 1 second cooldown
	int tickDelay = handle->tickDelay;
	if (tickDelay <= 0) tickDelay = 50; // Prevent division by zero or negative tick delays, assume 20Hz if invalid
	unsigned long delayTicks = delayMillis / tickDelay;
	player->spawnCooldownActive = true;
	player->spawnCooldownEndTick = handle->tick + delayTicks;
}

void World::frozenUpdate() {
	for (auto player : this->players) {
		auto router = player->router;
		router->splitAttempts = 0;
		router->ejectAttempts = 0;
		if (router->isPressingQ) {
			if (!router->hasPressedQ)
				router->onQPress();
			router->hasPressedQ = true;
		} else router->hasPressedQ = false;
		router->requestingSpectate = false;
		router->spawningName = "";
	}
}

void World::liveUpdate() {

	/*
	if (stats.loadTime > 80.0f) {
		Logger::warn(string("Server can't keep up! Load: ") + std::to_string(stats.loadTime) + "%");
		handle->bench = true;
	} */

	Stopwatch bench;
	bench.begin();

	handle->gamemode->onWorldTick(this);
	for (auto c : cells) c->onTick();

	handle->timing.tickCells = bench.lap();

	unsigned int diff = handle->runtime.pelletCount - pelletCount;
	bool failed = false;
	while (diff-- > 0) {
		float spawnSize = handle->runtime.pelletMinSize;
		auto pos = getSafeSpawnPos(spawnSize, failed);
		if (!failed) addCell(new Pellet(this, this, pos.getX(), pos.getY()));
	}

	diff = handle->runtime.virusMinCount - virusCount;
	while (diff-- > 0) {
		float spawnSize = handle->runtime.virusSize + 200.0f;
		auto pos = getSafeSpawnPos(spawnSize, failed);
		if (!failed) addCell(new Virus(this, pos.getX(), pos.getY()));
	}

	diff = handle->runtime.mothercellCount - motherCellCount;
	while (diff-- > 0) {
		float spawnSize = handle->runtime.mothercellSize + 200.0f;
		auto pos = getSafeSpawnPos(spawnSize, failed);
		if (!failed) addCell(new MotherCell(this, pos.getX(), pos.getY()));
	}

	handle->timing.spawnCell = bench.lap();

	list<pair<Cell*, Cell*>> rigid;
	list<pair<Cell*, Cell*>> eat;

	atomic<unsigned int> max_query_per_cell = 0;
	atomic<unsigned int> queries = 0;
	unsigned int insides = 0;

	for (auto c : cells) {
		if (c->getType() == CellType::PELLET) continue;
		boostCell(c);
		if (c->inside) {
			c->inside = false;
			insides++;
		}
	}

	handle->timing.insides = static_cast<float>(insides);
	handle->timing.boostCell = bench.lap();
	
	for (auto c : cells) {
		if (c->getType() != CellType::PLAYER) continue;
		auto pc = static_cast<PlayerCell*>(c);
		movePlayerCell(pc);
		decayPlayerCell(pc);
		autosplitPlayerCell(pc);
		bounceCell(pc);
		updateCell(pc);
	}

	handle->timing.updatePC = bench.lap();
	
	std::mutex mtx;

	//cells.sort([](Cell* a, Cell* b) {
	//	return (a->getSize() - a->getAge() * 0.1f) > (b->getSize() - b->getAge() * 0.1f);
	//});

	handle->timing.sortCell = bench.lap();

	for (int offset = 0; offset < handle->runtime.physicsThreads; offset++) {
		physicsPool->enqueue([this, offset, &rigid, &eat, &mtx, &queries, &max_query_per_cell]() {

			list<pair<Cell*, Cell*>> thread_rigid;
			list<pair<Cell*, Cell*>> thread_eat;

			auto index = offset;
			auto start = cells.cbegin();
			std::advance(start, offset);

			while (index < cells.size()) {
				auto c = *start;
				std::advance(start, handle->runtime.physicsThreads);
				index += handle->runtime.physicsThreads;

				if (c->getType() == CellType::PELLET || c->inside ||
			//		c->getType() == CellType::VIRUS  ||
					(c->getType() == CellType::EJECTED_CELL && 
					 (c->getAge() <= 1 || !c->isBoosting()))) continue;

				auto q = finder->search(c->range, [&c, &thread_rigid, &thread_eat](auto o) {
					auto other = (Cell*) o;
					if (!other->exist) return false;
					if (c->id == other->id) return false;

					auto dx = c->getX() - other->getX();
					auto dy = c->getY() - other->getY();
					auto dSq = dx * dx + dy * dy;

					if (c->getSize() > other->getSize()) {
						if (dSq < c->getSize()) other->inside = true;
					} else {
						if (dSq < other->getSize()) c->inside = true;
					}

					switch (c->getEatResult(other)) {
						case EatResult::COLLIDE:
							thread_rigid.push_back(std::make_pair(c, other));
							return true;
						case EatResult::EAT:
							thread_eat.push_back(std::make_pair(c, other));
							return false;
						case EatResult::EATINVD:
							thread_eat.push_back(std::make_pair(other, c));
							return false;
                        case EatResult::NONE:
							return false;
						default:
							return false;
					}
				});
				queries += q;
				max_query_per_cell = std::max(max_query_per_cell.load(), q);
			}

			mtx.lock();
			rigid.splice(rigid.begin(), thread_rigid);
			eat.splice(eat.begin(), thread_eat);
			mtx.unlock();
		});
	}

	physicsPool->waitFinished();

	handle->timing.quadTree = bench.lap();

	for (auto [r1, r2] : rigid)
		resolveRigidCheck(r1, r2);

	handle->timing.rgdCheck = bench.lap();

	eat.sort([](pair<Cell*, Cell*> pairA, pair<Cell*, Cell*> pairB) { 
		return (pairA.first->getSize() - pairA.first->getAge() * 0.1f) > 
		       (pairB.first->getSize() - pairB.first->getAge() * 0.1f);
	});

	for (auto [c1, c2] : eat)
		resolveEatCheck(c1, c2);

	handle->timing.eatCheck = bench.lap();

	auto r_iter = cells.begin();
	while (r_iter != cells.cend())
		if (!(*r_iter)->exist) r_iter = cells.erase(r_iter);
		else r_iter++;

	largestPlayer = nullptr;
	for (auto p : players)
		if (p->score > 0 && (!largestPlayer || p->score > largestPlayer->score))
			largestPlayer = p;

	auto p_iter = players.begin();
	while (p_iter != players.cend()) {

		auto original_player_in_loop = *p_iter;
		if (!original_player_in_loop->exist()) {
			p_iter = players.erase(p_iter);
			continue;
		}
		p_iter++;

		if (original_player_in_loop->state == PlayerState::SPEC && !largestPlayer)
			original_player_in_loop->updateState(PlayerState::ROAM);

		Router* source_input_router = original_player_in_loop->router; 
		if (!source_input_router) continue;

		Player* player_to_act = original_player_in_loop;
		Router* effective_router = source_input_router;

		// Dual control logic: if owner is controlling dual, redirect action checks
		if (original_player_in_loop->m_playerType == PlayerType::REGULAR &&
			original_player_in_loop->m_isDualActive &&
			original_player_in_loop->m_dualPlayer &&
			original_player_in_loop->m_dualPlayer->exist()) {
			
			player_to_act = original_player_in_loop->m_dualPlayer;
			
			if (player_to_act->router && source_input_router != player_to_act->router) { 
				player_to_act->router->mouseX.store(source_input_router->mouseX.load());
				player_to_act->router->mouseY.store(source_input_router->mouseY.load());
				// Other flags like splitAttempts, ejectAttempts, isPressingQ are already read from source_input_router
				// in the action conditions later in this loop.
			} 
		} else if (original_player_in_loop->m_playerType == PlayerType::DUAL_MINION) {
			continue; 
		}

        if (source_input_router) { 
            Logger::info("[P_ACT_DET] player_to_act ID: " + std::to_string(player_to_act->id) + 
                         ", source_input_router tied to player ID: " + std::to_string(source_input_router->player ? source_input_router->player->id : 0) +
                         ", source_input_router->splitAttempts: " + std::to_string(source_input_router->splitAttempts.load()));
        }

		// --- BEGIN SPLIT LOGIC CHECK ---
        Logger::info("[SPLIT_CHECK_ENTRY] Player ID: " + std::to_string(player_to_act->id) + ", source_router splitAttempts BEFORE while: " + std::to_string(source_input_router ? source_input_router->splitAttempts.load() : -1));
        
        int splits_this_tick = 0; // Counter for splits performed in this game tick
        while (source_input_router && source_input_router->splitAttempts.load() > 0 && splits_this_tick < handle->runtime.playerSplitCap) {
            Logger::info("[SPLIT_WHILE_TOP] Player ID: " + std::to_string(player_to_act->id) + ", source_router splitAttempts AT START OF WHILE: " + std::to_string(source_input_router->splitAttempts.load()) + ", splits_this_tick: " + std::to_string(splits_this_tick) + ", playerSplitCap: " + std::to_string(handle->runtime.playerSplitCap));
            source_input_router->splitAttempts--; 
            Logger::info("[SPLIT_ATTEMPT_DEC] Player ID: " + std::to_string(player_to_act->id) + ", source_router splitAttempts AFTER DECREMENT: " + std::to_string(source_input_router->splitAttempts.load()));

            bool can_split_this_iteration = false;
            if (player_to_act->ownedCells.size() < handle->runtime.playerMaxCells) {
                int splittable_cells = 0;
                Logger::info("[SPLIT_OWNED_LOOP_ENTRY] Player ID: " + std::to_string(player_to_act->id) + ". Owned cell count: " + std::to_string(player_to_act->ownedCells.size()));
                for (auto cell : player_to_act->ownedCells) {
                    if (!cell) {
                        Logger::error("[SPLIT_CELL_NULL] Player ID: " + std::to_string(player_to_act->id));
                        continue; 
                    }
                    Logger::info("[SPLIT_CELL_CHECK] Player ID: " + std::to_string(player_to_act->id) + ", Cell ID: " + std::to_string(cell->id) + " with size: " + std::to_string(cell->getSize()));
                    if (cell->getSize() >= handle->runtime.playerMinSplitSize) {
                        splittable_cells++;
                    }
                }
                Logger::info("[SPLIT_OWNED_LOOP_EXIT] Player ID: " + std::to_string(player_to_act->id) + ". Splittable cells found: " + std::to_string(splittable_cells));
                if (player_to_act->ownedCells.size() + splittable_cells <= handle->runtime.playerMaxCells && splittable_cells > 0) {
                    can_split_this_iteration = true;
                } else {
                     Logger::info("[SPLIT_CANNOT_CONDITION] Player ID: " + std::to_string(player_to_act->id) + 
                                  " Cell count limit / no splittable. Owned: " + std::to_string(player_to_act->ownedCells.size()) + 
                                  ", Splittable: " + std::to_string(splittable_cells) + 
                                  ", MaxCells: " + std::to_string(handle->runtime.playerMaxCells));
                }
            } else {
                 Logger::info("[SPLIT_CANNOT_MAX_CELLS] Player ID: " + std::to_string(player_to_act->id) + 
                              " Already at max cell count. Owned: " + std::to_string(player_to_act->ownedCells.size()) + 
                              ", MaxCells: " + std::to_string(handle->runtime.playerMaxCells));
            }

            if (can_split_this_iteration) {
                Logger::info("[SPLIT_CAN_SPLIT_TRUE] Player ID: " + std::to_string(player_to_act->id) + ". Proceeding with splitPlayer call.");
                player_to_act->justPopped = false;
                splitPlayer(player_to_act); 
                splits_this_tick++; // Increment counter for actual splits performed
            } else {
                Logger::info("[SPLIT_CAN_SPLIT_FALSE] Player ID: " + std::to_string(player_to_act->id) + ". Breaking split attempt loop for this player.");
                break; 
            }
            Logger::info("[SPLIT_WHILE_BOTTOM] Player ID: " + std::to_string(player_to_act->id) + ", source_router splitAttempts: " + std::to_string(source_input_router->splitAttempts.load()) + ". Loop will continue if > 0.");
        }
        Logger::info("[SPLIT_CHECK_EXIT] Player ID: " + std::to_string(player_to_act->id) + ", source_router splitAttempts AFTER while: " + std::to_string(source_input_router ? source_input_router->splitAttempts.load() : -1));
        // --- END SPLIT LOGIC CHECK ---

		auto nextEjectTick = handle->tick - handle->runtime.playerEjectDelay;
		if ((effective_router->ejectAttempts > 0 || effective_router->ejectMacro) && nextEjectTick >= effective_router->ejectTick) {
			effective_router->attemptEject();
			effective_router->ejectAttempts = 0;
			effective_router->ejectTick = handle->tick;
		}

		if (effective_router->isPressingQ) {
			if (!effective_router->hasPressedQ) {
				// If source_input_router had hasPressedQ=true because Q was just pressed,
				// and we transferred isPressingQ=true, this ensures onQPress is called for dual.
				// If isPressingQ was false on source, this won't run.
				effective_router->onQPress(); 
			}
			effective_router->hasPressedQ = true;
		} else {
			effective_router->hasPressedQ = false;
			// If Q was just released on owner, ensure source_input_router->hasPressedQ is also false.
			// This is already handled by this else block if source_input_router == effective_router.
			// If they are different (dual active), source_input_router->hasPressedQ should also be false.
			if (source_input_router != effective_router) {
			    source_input_router->hasPressedQ = false;
			}
		}

		// Spectate and Spawn requests should probably only be handled by the original_player_in_loop's router (source_input_router)
		// as these are meta-actions tied to the client connection, not direct game control.
		if (source_input_router->requestingSpectate)
			source_input_router->onSpectateRequest();

		if (source_input_router->requestSpawning)
			source_input_router->onSpawnRequest();

		// player->updateViewArea() was here. It should be called for the original_player_in_loop
		// as view area calculation already considers dual player cells based on original_player_in_loop's dual status.
		original_player_in_loop->updateViewArea();
	}

	handle->timing.queryOPs = static_cast<float>(queries.load());
	handle->timing.viewarea = bench.lap();
	handle->timing.totalCells = static_cast<float>(cells.size());

	compileStatistics();
	handle->gamemode->compileLeaderboard(this);

	if (stats.external <= 0)
		if (handle->worlds.size() > handle->runtime.worldMinCount)
			toBeRemoved = true;
}

void World::resolveRigidCheck(Cell* a, Cell* b) {
	if (a->getAge() <= 1 || b->getAge() <= 1) return;
	float dx = b->getX() - a->getX();
	float dy = b->getY() - a->getY();
	float d = sqrtf(dx * dx + dy * dy); // Using sqrtf from cmath
	float m = a->getSize() + b->getSize() - d;
	if (m <= 0) return;
	if (fabsf(d) < 1e-5f) d = 1.f, dx = 1.f, dy = 0.f; // Avoid division by zero if d is very small
	else dx /= d, dy /= d;
	
	float dampening = 1.f;
	
	float M = a->getSquareSize() + b->getSquareSize();
	float aM = b->getSquareSize() / M;
	float bM = a->getSquareSize() / M;
	
	// Removed the original logic that reduced the push on the LARGER cell.
	// This means larger cells will now be pushed more by smaller cells than before,
	// and smaller cells will be pushed by larger cells according to mass ratios without special reduction.

	// if (a->getSize() > b->getSize() * 2.0f) {
	// 	aM *= 0.13f;
	// } else if (b->getSize() > a->getSize() * 2.0f) {
	// 	bM *= 0.13f;
	// }
	
	float push_dist_val_a = std::min(m, a->getSize());
	float push_dist_val_b = std::min(m, b->getSize());

	float final_push_ax = -dx * push_dist_val_a * aM * dampening;
	float final_push_ay = -dy * push_dist_val_a * aM * dampening;
	float final_push_bx =  dx * push_dist_val_b * bM * dampening;
	float final_push_by =  dy * push_dist_val_b * bM * dampening;

	// New logic: If one cell is significantly larger, make it push the smaller cell more, and be pushed less itself.
	float sizeThresholdFactor = 1.9f; 
	float pushBoostFactor = 1.8f;   // How much more the smaller cell is pushed
	float bigCellResistanceFactor = 0.06f; // Big cell is pushed by only 1% of the original force (almost not at all)

	if (a->getSize() > b->getSize() * sizeThresholdFactor) { // a is big, b is small
		// Increase push on small cell b
		final_push_bx *= pushBoostFactor;
		final_push_by *= pushBoostFactor;
		// Drastically Decrease push on big cell a (make it very resistant)
		final_push_ax *= bigCellResistanceFactor;
		final_push_ay *= bigCellResistanceFactor;
	} else if (b->getSize() > a->getSize() * sizeThresholdFactor) { // b is big, a is small
		// Increase push on small cell a
		final_push_ax *= pushBoostFactor;
		final_push_ay *= pushBoostFactor;
		// Drastically Decrease push on big cell b (make it very resistant)
		final_push_bx *= bigCellResistanceFactor;
		final_push_by *= bigCellResistanceFactor;
	}

	// ---- BEGIN MODIFICATION FOR LINESPLIT-LIKE BEHAVIOR & FEED EXCEPTION ----
	if (a->owner && b->owner && a->owner == b->owner) { // Player owns both cells
		Player* commonOwner = a->owner;
		Router* router = commonOwner->router;
		
		bool useProjection = false; // Master flag to decide if projection physics apply this tick
		unsigned long currentTick = handle->tick;

		bool genericLockOn = commonOwner->isLineLocked.load();
		bool specialLockOn = commonOwner->specialLineSplitLockActive;

		if (specialLockOn && genericLockOn) {
			// Logger::debug("[ResolveRigidCheck] Player " + std::to_string(commonOwner->id) + ": Special & Generic Lock ON. Will attempt UNTIMED projection.");
			useProjection = true;
		} else if (genericLockOn) { // Special lock is OFF, only Generic lock is ON
			// Logger::debug("[ResolveRigidCheck] Player " + std::to_string(commonOwner->id) + ": Only Generic Lock ON. Checking timed conditions...");
			if (commonOwner->needsProjectionReactivation) {
				// Logger::debug("[ResolveRigidCheck] Player " + std::to_string(commonOwner->id) + ": Generic Lock requires reactivation. No projection.");
				useProjection = false;
			} else if (commonOwner->projectionActiveUntilTick != 0 && currentTick > commonOwner->projectionActiveUntilTick) {
				Logger::info("[ResolveRigidCheck] Player " + std::to_string(commonOwner->id) + ": Generic Lock 1-second window EXPIRED (current: " + std::to_string(currentTick) + ", until: " + std::to_string(commonOwner->projectionActiveUntilTick) + "). Reactivation needed. No projection.");
				commonOwner->needsProjectionReactivation = true; // Mark for reactivation
				useProjection = false;
			} else {
				// Logger::debug("[ResolveRigidCheck] Player " + std::to_string(commonOwner->id) + ": Generic Lock active within 1s window. Will attempt TIMED projection.");
				useProjection = true;
			}
		} else {
			// Neither special lock is active, nor is generic lock by itself. No projection.
			// Logger::debug("[ResolveRigidCheck] Player " + std::to_string(commonOwner->id) + ": No relevant locks active for projection.");
			useProjection = false;
		}

		// If any projection type is potentially active, perform common safety checks
		if (useProjection) {
			if (!router) {
				Logger::debug("[ResolveRigidCheck] Router is null for player " + std::to_string(commonOwner->id) + ". Projection aborted.");
				useProjection = false;
				// If this was due to a generic timed lock attempt, it now needs reactivation.
				if (genericLockOn && !specialLockOn) commonOwner->needsProjectionReactivation = true;
			}

			if (useProjection) { // Re-check after router check
				const unsigned long FEED_MAX_AGE_TICKS = (handle->tickDelay > 0) ? (250 / handle->tickDelay) : 5;
				bool isRecentFeedCollision = false;
				if (a->getType() == PLAYER && b->getType() == EJECTED_CELL && b->owner == commonOwner && b->getAge() < FEED_MAX_AGE_TICKS) isRecentFeedCollision = true;
				else if (b->getType() == PLAYER && a->getType() == EJECTED_CELL && a->owner == commonOwner && a->getAge() < FEED_MAX_AGE_TICKS) isRecentFeedCollision = true;

				if (isRecentFeedCollision) {
					Logger::debug("[ResolveRigidCheck] Player " + std::to_string(commonOwner->id) + " collision with own recent feed. Projection aborted.");
					useProjection = false;
					// If this was due to a generic timed lock attempt, it now needs reactivation.
					if (genericLockOn && !specialLockOn) commonOwner->needsProjectionReactivation = true;
				}
			}
		}
		
	} // End of owner check block
	// ---- END MODIFICATION ----

	a->setX(a->getX() + final_push_ax);
	a->setY(a->getY() + final_push_ay);
	b->setX(b->getX() + final_push_bx);
	b->setY(b->getY() + final_push_by);
	
	bounceCell(a);
	bounceCell(b);
	updateCell(a);
	updateCell(b);
}

void World::resolveEatCheck(Cell* a, Cell* b) {
	if (!a->exist || !b->exist) return;

    // --- BEGIN DUAL PLAYER EAT IMMUNITY (REMOVED) ---
    /*
    if (a->owner && b->owner && a->getType() == CellType::PLAYER && b->getType() == CellType::PLAYER) {
        Player* ownerA = a->owner;
        Player* ownerB = b->owner;

        // Check if A is dual of B, or B is dual of A
        bool isDualPair = false;
        // Check if ownerA has ownerB as dual, and ownerB has ownerA as main
        if (ownerA->m_playerType == PlayerType::REGULAR && ownerA->m_dualPlayer == ownerB && ownerB->m_playerType == PlayerType::DUAL_MINION && ownerB->m_ownerPlayer == ownerA) {
            isDualPair = true;
        // Check if ownerB has ownerA as dual, and ownerA has ownerB as main
        } else if (ownerB->m_playerType == PlayerType::REGULAR && ownerB->m_dualPlayer == ownerA && ownerA->m_playerType == PlayerType::DUAL_MINION && ownerA->m_ownerPlayer == ownerB) {
            isDualPair = true;
        }

        if (isDualPair) {
            return; // Owner and dual minion cannot eat each other
        }
    }
    */
    // --- END DUAL PLAYER EAT IMMUNITY (REMOVED) ---

	float dx_dist = b->getX() - a->getX();
	float dy_dist = b->getY() - a->getY();
	float d_dist = sqrt(dx_dist * dx_dist + dy_dist * dy_dist);
	if (d_dist > a->getSize() - b->getSize() / handle->runtime.worldEatOverlapDiv) return;
	if (!handle->gamemode->canEat(a, b)) return;

	// --- BEGIN USER'S BOOST ABSORPTION LOGIC (Snippet 1) ---
	bool can_absorb_boost = (b->boost.d > 0.0f);

	if (!can_absorb_boost) {
		// Eaten cell (b) has no significant boost to transfer, or other condition for "normal" eat.
	} else {
		// Eaten cell (b) has boost, try to transfer/absorb some to/by eater (a)
		float current_a_size = a->getSize();
		float b_size = b->getSize();
		float ratio = 0.0f;
		if (current_a_size + 100.0f != 0.0f) { // Avoid division by zero
			 ratio = b_size / (current_a_size + 100.0f);
		}

		a->boost.d += ratio * 0.01 * b->boost.d;

		float playerMaxBoostSetting = handle->runtime.playerSplitBoost; // Using existing setting for T.PLAYER_MAX_BOOST
		if (a->boost.d >= playerMaxBoostSetting) {
			a->boost.d = playerMaxBoostSetting;
		}
		if (a->boost.d < 0.0f) { // Ensure boost magnitude is not negative
			a->boost.d = 0.0f;
		}

		float new_boost_ax = a->boost.dx + ratio * 0.050f * b->boost.dx;
		float new_boost_ay = a->boost.dy + ratio * 0.050f * b->boost.dy;
		float norm_sq = new_boost_ax * new_boost_ax + new_boost_ay * new_boost_ay;

		if (norm_sq > 1e-9f) { // Check norm_sq is non-zero (use epsilon for float comparison)
			float norm_inv = 1.0f / sqrtf(norm_sq);
			a->boost.dx = new_boost_ax * norm_inv;
			a->boost.dy = new_boost_ay * norm_inv;
		}
		// If norm_sq is zero, old a->boost.dx/dy are preserved.
	}
	// cell->flag |= UPDATE_BIT; is effectively handled by updateCell(a) later.
	// TODO: Implement `other->eatenByID = cell_id(cell);` (requires Cell class modification for eatenByID).
	// Example: uint32_t eater_id_for_b = a->id;
	// --- END USER'S BOOST ABSORPTION LOGIC ---

	a->whenAte(b);
	b->whenEatenBy(a);
	removeCell(b);
	updateCell(a);
}

void World::boostCell(Cell* cell) {
	if (!cell->isBoosting()) return;
	float d = cell->boost.d / 9 * handle->stepMult;
	cell->setX(cell->getX() + cell->boost.dx * d);
	cell->setY(cell->getY() + cell->boost.dy * d);
	bounceCell(cell, true);
	updateCell(cell);
	cell->boost.d -= d;
}

void World::bounceCell(Cell* cell, bool bounce) {
	float r = cell->getSize() / 2.0;
	if (cell->getX() <= border.getX() - border.w + r) {
		cell->setX(border.getX() - border.w + r);
		if (bounce) cell->boost.dx = -cell->boost.dx;
	}
	if (cell->getX() >= border.getX() + border.w - r) {
		cell->setX(border.getX() + border.w - r);
		if (bounce) cell->boost.dx = -cell->boost.dx;
	}
	if (cell->getY() <= border.getY() - border.h + r) {
		cell->setY(border.getY() - border.h + r);
		if (bounce) cell->boost.dy = -cell->boost.dy;
	}
	if (cell->getY() >= border.getY() + border.h - r) {
		cell->setY(border.getY() + border.h - r);
		if (bounce) cell->boost.dy = -cell->boost.dy;
	}
}

void World::splitVirus(Virus* virus) {
	auto newVirus = new Virus(this, virus->getX(), virus->getY());
	newVirus->boost.dx = sin(virus->splitAngle);
	newVirus->boost.dy = cos(virus->splitAngle);
	newVirus->boost.d = handle->runtime.virusSplitBoost;
	addCell(newVirus);
}

void World::movePlayerCell(PlayerCell* cell) {
	if (!cell->owner) return;
	auto router = cell->owner->router;
	if (router->disconnected) return;

	// Get target coordinates from router
	float mouseX = router->mouseX;
	float mouseY = router->mouseY;

	// Calculate direction and distance
	float dx = mouseX - cell->getX();
	float dy = mouseY - cell->getY();
	float d = sqrt(dx * dx + dy * dy);
	if (d < 1) return; // No movement if already at target
	dx /= d;
	dy /= d;

	// Calculate speed based on new formula
	const float baseSpeedModifier = 88.0f; // Kept original base modifier for now
	const float sizeExponent = -0.39f;     // New exponent from template
	float multi = handle->runtime.playerMoveMult;
	float speed = baseSpeedModifier * pow(cell->getSize(), sizeExponent) * multi;

	// Calculate movement step for this tick
	float dt = handle->stepMult; // Using stepMult as delta time factor
	float m = std::min(speed, d) * dt;

	// Apply movement
	cell->setX(cell->getX() + dx * m);
	cell->setY(cell->getY() + dy * m);

	// ---- BEGIN LINELOCK MODIFICATION ----
	if (cell->owner && cell->owner->isLineLocked && fabsf(cell->owner->lineEqDenomInv) > 1e-5f && (cell->cellFlags & LOCK_BIT)) {
		Player* owner = cell->owner;
		float x0 = cell->getX();
		float y0 = cell->getY();

		// Project (x0, y0) onto the line Ax + By + C = 0
		// projX = (B*(B*x0 - A*y0) - A*C) / (A^2 + B^2)
		// projY = (A*(-B*x0 + A*y0) - B*C) / (A^2 + B^2)
		float projectedX = (owner->lineEqB * (owner->lineEqB * x0 - owner->lineEqA * y0) - owner->lineEqA * owner->lineEqC) * owner->lineEqDenomInv;
		float projectedY = (owner->lineEqA * (-owner->lineEqB * x0 + owner->lineEqA * y0) - owner->lineEqB * owner->lineEqC) * owner->lineEqDenomInv;

cell->setX(projectedX);
	cell->setY(projectedY);
	}
	// ---- END LINELOCK MODIFICATION ----

	// Removed old modifier logic for newly split cells to match template structure
	// float modifier = 1.0f; 
	// if (cell->getSize() < handle->runtime.playerMinSplitSize * 5.0f && 
	// 	cell->getAge() <= handle->runtime.playerNoCollideDelay) modifier = -1.0f;
	// float m = std::min(cell->getMoveSpeed() * modifier, d) * handle->stepMult;
	// cell->setX(cell->getX() + dx * m);
	// cell->setY(cell->getY() + dy * m);
}

void World::decayPlayerCell(PlayerCell* cell) {
	float newSize = cell->getSize() - cell->getSize() * handle->gamemode->getDecayMult(cell) / 50 * handle->stepMult;
	float minSize = handle->runtime.playerMinSize;
	cell->setSize(std::max(newSize, minSize));
}

void World::launchPlayerCell(PlayerCell* cell, float size, Boost& boost) {
	cell->setSquareSize(cell->getSquareSize() - size * size);
	float x = cell->getX() + handle->runtime.playerSplitDistance * boost.dx;
	float y = cell->getY() + handle->runtime.playerSplitDistance * boost.dy;
	auto newCell = new PlayerCell(this, cell->owner, x, y, size);
	newCell->boost = boost;
	addCell(newCell);
}

void World::autosplitPlayerCell(PlayerCell* cell) {
	if (!cell->owner) return;
	float minSplit = handle->runtime.playerMaxSize * handle->runtime.playerMaxSize;
	int cellsLeft = 1 + handle->runtime.playerMaxCells - cell->owner->ownedCells.size();
	float size = cell->getSquareSize();
	int overflow = ceil(size / minSplit);
	if (overflow == 1 || cellsLeft <= 0) return;
	float splitTimes = std::min(overflow, cellsLeft);
	float splitSize = std::min(sqrt(size / splitTimes), handle->runtime.playerMaxSize);
	for (int i = 0; i < splitTimes; i++) {
		auto angle = randomZeroToOne * 2 * PI;
		Boost boost { (float) sin(angle), (float) cos(angle), handle->runtime.playerSplitBoost };
		launchPlayerCell(cell, splitSize, boost);
	}
	cell->setSize(splitSize);
}

void World::splitPlayer(Player* player) {
	auto router = player->router;
	int index = 0;
	auto originalLength = player->ownedCells.size();
	for (auto cell : player->ownedCells) {
		if (++index > originalLength) break;
		if (player->ownedCells.size() >= handle->runtime.playerMaxCells) return;
		if (cell->getSize() < handle->runtime.playerMinSplitSize) continue;
		
		float dir_dx = router->mouseX - cell->getX();
		float dir_dy = router->mouseY - cell->getY();
		float d_mag = sqrt(dir_dx * dir_dx + dir_dy * dir_dy);

		if (d_mag < 1.0f) {
			dir_dx = 1.0f;
			dir_dy = 0.0f;
		} else {
			dir_dx /= d_mag;
			dir_dy /= d_mag;
		}

		// --- REVERTED SPLIT LOGIC ---
		float new_cell_size = cell->getSize() / handle->runtime.playerSplitSizeDiv;
		// Optional: check against playerMinSize, though usually playerSplitSizeDiv handles it.
		// if (new_cell_size < handle->runtime.playerMinSize) {
		//     new_cell_size = handle->runtime.playerMinSize;
		// }

		Boost new_cell_boost { dir_dx, dir_dy, handle->runtime.playerSplitBoost };
		
		launchPlayerCell(cell, new_cell_size, new_cell_boost);
		// --- END REVERTED SPLIT LOGIC ---
	}
}

void World::ejectFromPlayer(Player* player) {
	if (player->justPopped) {
		handle->ticker.timeout(5, [player] { player->justPopped = false; });
		return;
	};
	float dispersion = handle->runtime.ejectDispersion;
	float loss = handle->runtime.ejectingLoss * handle->runtime.ejectingLoss;
	auto router = player->router;
	for (auto cell : player->ownedCells) {
		if (cell->getAge() < 3) continue;
		if (cell->getSize() < handle->runtime.playerMinEjectSize) continue;
		float dx = router->mouseX - cell->getX();
		float dy = router->mouseY - cell->getY();
		float d = sqrt(dx * dx + dy * dy);
		if (d < 1) dx = 1, dy = 0, d = 1;
		else dx /= d, dy /= d;
		float sx = cell->getX() + dx * cell->getSize();
		float sy = cell->getY() + dy * cell->getSize();
		auto newCell = new EjectedCell(this, player, sx, sy, cell->getColor());
		float a = atan2(dx, dy) - dispersion + randomZeroToOne * 2 * dispersion;
		newCell->boost.dx = sin(a);
		newCell->boost.dy = cos(a);
		newCell->boost.d = handle->runtime.ejectedCellBoost;
		addCell(newCell);
		cell->setSquareSize(cell->getSquareSize() - loss);
		updateCell(cell);
		ejectCount++;
	}
}

void World::popPlayerCell(PlayerCell* cell) {
	vector<float> dist;
	distributeCellMass(cell, dist);
	for (auto mass : dist) {
		float angle = randomZeroToOne * 2 * PI;
		Boost boost { sin(angle), cos(angle), handle->runtime.playerSplitBoost };
		launchPlayerCell(cell, sqrt(mass * 100), boost);
	}
	if (cell->owner) cell->owner->justPopped = true;
}

void World::distributeCellMass(PlayerCell* cell, std::vector<float>& dist) {
	auto player = cell->owner;
	float cellsLeft = handle->runtime.playerMaxCells - (player ? player->ownedCells.size() : 0);
	if (cellsLeft <= 0) return;
	float splitMin = handle->runtime.playerMinSplitSize;
	splitMin = splitMin * splitMin / 100;
	float cellMass = cell->getMass();

	if (handle->runtime.virusMonotonePops) {
		float amount = std::min(floor(cellMass / splitMin), cellsLeft);
		float perPiece = cellMass / (amount + 1.0);
		while (--amount >= 0) dist.push_back(perPiece);
		return;
	}

	if (cellMass / cellsLeft < splitMin) {
		float amount = 2.0, perPiece = 0;
		while ((perPiece = cellMass / (amount + 1.0)) >= splitMin && amount * 2 <= cellsLeft)
			amount *= 2.0;
		while (--amount >= 0) 
			dist.push_back(perPiece);
		return;
	}

	float nextMass = cellMass / 2.0;
	float massLeft = cellMass / 2.0;

	while (cellsLeft > 0) {
		if (nextMass / cellsLeft < splitMin) break;
		while (nextMass >= massLeft && cellsLeft > 1)
			nextMass /= 2.0;
		dist.push_back(nextMass);
		massLeft -= nextMass;
		cellsLeft--;
	}
	nextMass = massLeft / cellsLeft;
	while (--cellsLeft >= 0) dist.push_back(nextMass);
}

void World::compileStatistics() {
	unsigned short internal = 0, external = 0, playing = 0, spectating = 0;
	for (auto p : players) {
		if (!p->router->isExternal()) { internal++; continue; }
		external++;
		if (p->state == PlayerState::ALIVE) playing++;
		else if (p->state == PlayerState::SPEC || p->state == PlayerState::ROAM)
			spectating++;
	}
	stats.limit = handle->runtime.listenerMaxConnections;
	stats.internal = internal;
	stats.external = external;
	stats.playing = playing;
	stats.spectating = spectating;

	stats.name = handle->runtime.serverName;
	stats.gamemode = handle->gamemode->getName();
	stats.loadTime = handle->averageTickTime / handle->stepMult;
	stats.uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - handle->startTime).count();
}

