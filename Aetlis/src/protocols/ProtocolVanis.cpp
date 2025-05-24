#include "ProtocolVanis.h"

#include "../primitives/Reader.h"
#include "../primitives/Writer.h"
#include "../sockets/Connection.h"
#include "../sockets/Listener.h"
#include "../worlds/Player.h"
#include "../cells/Cell.h"
#include "../ServerHandle.h"
#include <cstdio>
#include <cmath>

const static char PONG_CHAR = 3;
const static string_view PONG = string_view(&PONG_CHAR, 1);

void ProtocolVanis::onSocketMessage(Reader& reader) {
	if (!reader.length()) return;

	unsigned char opCode = reader.readUInt8();
	if (connection->player) {
		printf("[SERVER LOG] Received OpCode: %u from client %u\n", opCode, connection->player->id);
	} else {
		printf("[SERVER LOG] Received OpCode: %u from pre-player connection\n", opCode);
	}
	switch (opCode) {
		// join
		case 1:
			connection->spawningName = reader.readStringUTF8();
			connection->spawningSkin = reader.readStringUTF8();
			connection->spawningTag  = reader.readStringUTF8();
			connection->requestSpawning = true;
			break;
		// spectate
		case 2:
			connection->requestingSpectate = true;
			if (reader.length() == 3) 
				connection->spectatePID = reader.readInt16();
			break;
		// ping
		case 3:
			{
				Writer writer;
				writer.writeUInt8(3);
				send(writer.finalize());
			}
			break;
		// toggle generic linelock (opcode 15)
		case 15:
			if (connection->player) {
				connection->player->isLineLocked = !connection->player->isLineLocked.load(); 
				if (connection->player->isLineLocked.load()) {
					Logger::debug("[ProtocolVanis] Player " + std::to_string(connection->player->id) + " toggled generic linelock ON.");
					// This timer is primarily for when generic lock is used WITHOUT special lock.
					// If special lock is active, World.cpp will ignore this timer.
					connection->player->needsProjectionReactivation = false;
					unsigned long genericProjectionDurationTicks = (connection->listener->handle->tickDelay > 0) ? (1000 / connection->listener->handle->tickDelay) : 20; // 1 second
					connection->player->projectionActiveUntilTick = connection->listener->handle->tick + genericProjectionDurationTicks;
					Logger::debug("[ProtocolVanis] Player " + std::to_string(connection->player->id) + " generic projection window set until tick: " + std::to_string(connection->player->projectionActiveUntilTick));
				} else {
					Logger::debug("[ProtocolVanis] Player " + std::to_string(connection->player->id) + " toggled generic linelock OFF.");
					connection->player->needsProjectionReactivation = true;
					connection->player->projectionActiveUntilTick = 0; 
				}
			}
			break;
		// mouse / unlock signal (opcode 16)
		case 16:
			connection->mouseX = reader.readInt32();
			connection->mouseY = reader.readInt32();
			if (connection->player) {
				// Opcode 16 from client's lockLinesplit(false) is a universal unlock
				if (connection->player->isLineLocked.load() || connection->player->specialLineSplitLockActive) {
					Logger::debug("[ProtocolVanis] Player " + std::to_string(connection->player->id) + " received OpCode 16 (mouse), UNLOCKING ALL LINELOCKS.");
					connection->player->isLineLocked = false;
					connection->player->specialLineSplitLockActive = false;
					connection->player->needsProjectionReactivation = true;
					connection->player->projectionActiveUntilTick = 0;
					// We don't reset specialLineSplitLockCooldownEndTick here; cooldown should run its course.
				}
			}
			break;
		// split //
		case 17:
			{
				unsigned char incoming_split_val = reader.readUInt8();
				if (incoming_split_val > 0) {
					connection->splitAttempts += incoming_split_val;
					const unsigned char MAX_SPLIT_ATTEMPTS = 8; 
					if (connection->splitAttempts > MAX_SPLIT_ATTEMPTS) {
						connection->splitAttempts = MAX_SPLIT_ATTEMPTS;
					}
                    if (connection->player) { // Ensure player exists for logging ID
                        Logger::info("[ProtocolVanis] Opcode 17: Player " + std::to_string(connection->player->id) + 
                                     " splitAttempts set to: " + std::to_string(connection->splitAttempts.load()));
                    }
				}
			}
			break;
		// Toggle Special Linesplit Lock (opcode 18)
		case 18:
			if (connection->player) {
				unsigned long currentTick = connection->listener->handle->tick;
				if (currentTick < connection->player->specialLineSplitLockCooldownEndTick) {
					Logger::debug("[ProtocolVanis] Player " + std::to_string(connection->player->id) + " attempted to activate Special Linesplit Lock WHILE ON COOLDOWN. Ignored.");
				} else {
					// Toggle on. Client logic for lockLinesplit(false) (opcode 16) will turn it off.
					// Client sends 15 then 18, so isLineLocked should be true.
					connection->player->specialLineSplitLockActive = true; 
					unsigned long specialLockCooldownTicks = (connection->listener->handle->tickDelay > 0) ? (1200 / connection->listener->handle->tickDelay) : 24; // ~1.2 seconds cooldown
					connection->player->specialLineSplitLockCooldownEndTick = currentTick + specialLockCooldownTicks;
					Logger::debug("[ProtocolVanis] Player " + std::to_string(connection->player->id) + " activated Special Linesplit Lock. Cooldown for next activation until: " + std::to_string(connection->player->specialLineSplitLockCooldownEndTick));
					// No need to manage needsProjectionReactivation or projectionActiveUntilTick here, as special lock bypasses them.
				}
			}
			break;
		// feed
		case 21:
			if (reader.length() == 1) {
				connection->ejectAttempts++;
				const unsigned char MAX_EJECT_ATTEMPTS = 7;
				if (connection->ejectAttempts > MAX_EJECT_ATTEMPTS) {
					connection->ejectAttempts = MAX_EJECT_ATTEMPTS;
				}
			} else {
				connection->ejectAttempts = 0;
				unsigned char macro = reader.readUInt8();
				connection->ejectMacro = macro > 0;
			}
			break;
		// Toggle Dual Control (opcode 23)
		case 23:
			if (connection->player && connection->player->m_playerType == PlayerType::REGULAR) {
				Player* owner = connection->player;
				if (owner->m_dualPlayer) { // Only allow toggle if dual player exists
					bool newDualActiveState = !owner->m_isDualActive;
					
					// If activating the dual player and it has no cells, spawn it.
					if (newDualActiveState && owner->m_dualPlayer->ownedCells.empty() && owner->world && owner->handle) {
						Logger::info("[ProtocolVanis] Player " + std::to_string(owner->id) + " activating dual player " + std::to_string(owner->m_dualPlayer->id) + ". Spawning dual player cells.");
						bool spawn_failed = false;
						float spawn_size = owner->handle->runtime.playerSpawnSize;
						// Attempt to spawn near the owner player, or a random position if that fails or is not preferred.
						Point center_of_view(owner->viewArea.getX(), owner->viewArea.getY());
						SpawnResult initial_spawn_result = owner->world->getPlayerSpawnNearPoint(center_of_view, spawn_size, spawn_failed);
						Point spawn_pos = initial_spawn_result.pos;

						if (spawn_failed) {
						    SpawnResult fallback_spawn_result = owner->world->getPlayerSpawn(spawn_size, spawn_failed); 
                            spawn_pos = fallback_spawn_result.pos;
						}
						if (!spawn_failed) {
						    owner->world->spawnPlayer(owner->m_dualPlayer, spawn_pos, spawn_size);
						} else {
						    Logger::error("[ProtocolVanis] Failed to find a spawn location for dual player " + std::to_string(owner->m_dualPlayer->id));
						    // Not setting dual active if spawn failed, or send error to client?
                        // For now, we'll proceed to setDualActive, client will see no cells if spawn failed.
						}
					}

					owner->setDualActive(newDualActiveState);
					// setDualActive should internally call sendDualPlayerUpdate
					Logger::info("[ProtocolVanis] Player " + std::to_string(owner->id) + " toggled dual control. Active is dual: " + std::to_string(owner->m_isDualActive));
				} else {
					Logger::warn("[ProtocolVanis] Player " + std::to_string(connection->player->id) + " attempted to toggle dual control, but no dual player exists.");
					// Optionally, send a default state back or an error
					sendDualPlayerUpdate(connection->player); // Send current state (no dual, main active)
				}
			} else {
				Logger::warn("[ProtocolVanis] Received opcode 23 from client without valid player or from a DUAL_MINION.");
			}
			break;
		// Request Create Dual Player (opcode 25)
		case 25:
			if (connection->player && connection->player->m_playerType == PlayerType::REGULAR) {
				Logger::info("[ProtocolVanis] Player " + std::to_string(connection->player->id) + " requested to create dual player.");
				connection->player->createDualPlayer(); 
				// createDualPlayer should internally call sendDualPlayerUpdate to notify client
			} else {
				Logger::warn("[ProtocolVanis] Received opcode 25 (RequestCreateDual) from client without valid player or from a DUAL_MINION.");
			}
			break;
		// chat
		case 99:
			connection->onChatMessage(reader.buffer());
			break;
	}
};

void ProtocolVanis::onChatMessage(ChatSource& source, string_view message) {
	Writer writer;
	writer.writeUInt8(0xd);
	writer.writeUInt16(source.pid);
	writer.writeBuffer(message);
	writer.writeUInt16(0);
	printf("[SERVER LOG] Sending OpCode: 13 (Chat Message)\n");
	send(writer.finalize());
};

void ProtocolVanis::onPlayerSpawned(Player* player) {
	if (player == connection->player) {

		player->lastVisibleCellData.clear();
		player->lastVisibleCells.clear();
		player->visibleCellData.clear();
		player->visibleCells.clear();

		Writer writer_spawn_confirm;
		writer_spawn_confirm.writeUInt8(0x12);
		printf("[SERVER LOG] Sending OpCode: 18 (Spawn Confirm) to player %u\n", player->id);
		send(writer_spawn_confirm.finalize());
	}
	Writer writer_player_info;
	writer_player_info.writeUInt8(15);
	writer_player_info.writeUInt16(player->id);
	writer_player_info.writeStringUTF8(player->chatName.c_str());
	writer_player_info.writeStringUTF8(player->cellSkin.c_str());
	printf("[SERVER LOG] Sending OpCode: 15 (Player Info) for player %u\n", player->id);
	send(writer_player_info.finalize());
};

void ProtocolVanis::onNewOwnedCell(PlayerCell* cell) {};

void ProtocolVanis::onNewWorldBounds(Rect* border, bool includeServerInfo) {
	Writer writer;
	writer.writeUInt8(1);
	writer.writeUInt8(2);
	writer.writeUInt8(connection->listener->handle->gamemode->getType());
	writer.writeUInt16(42069);
	writer.writeUInt16(connection->player->id);
	writer.writeUInt32(border->w * 2);
	writer.writeUInt32(border->h * 2);
	printf("[SERVER LOG] Sending OpCode: 1 (Initial World Bounds/Player Data) to player %u\n", connection->player->id);
	send(writer.finalize());
	if (!connection->hasPlayer || !connection->player->hasWorld) return;

	for (auto p : connection->player->world->players)
		if (p != connection->player) onPlayerSpawned(p);
};

void ProtocolVanis::onWorldReset() {
};

void ProtocolVanis::onDead() {
	Writer writer;
	writer.writeUInt8(0x14);
	writer.writeUInt16((connection->listener->handle->tick -
		connection->player->joinTick) * connection->listener->handle->tickDelay / 1000);
	writer.writeUInt16(connection->player->killCount);
	writer.writeUInt32(connection->player->maxScore);
	printf("[SERVER LOG] Sending OpCode: 20 (Dead) to player %u\n", connection->player->id);
	send(writer.finalize());
}

void ProtocolVanis::onLeaderboardUpdate(LBType type, vector<LBEntry*>& entries, LBEntry* selfEntry) {
	if (type == LBType::FFA) {
		Writer writer;
		writer.writeUInt8(0xb);
		unsigned char count = 0;
		for (auto entry : entries) {
			count++;
			if (count > 10) break;
			writer.writeUInt16(((FFAEntry*)entry)->pid);
		}
		writer.writeUInt16(0);
		printf("[SERVER LOG] Sending OpCode: 11 (Leaderboard FFA)\n");
		send(writer.finalize());
	}
};

void ProtocolVanis::onSpectatePosition(ViewArea* area) {
	Writer writer;
	writer.writeUInt8(0x11);
	writer.writeInt32(area->getX());
	writer.writeInt32(area->getY());
	printf("[SERVER LOG] Sending OpCode: 17 (Spectate Position)\n");
	send(writer.finalize());
};

void ProtocolVanis::onMinimapUpdate() {
	auto world = connection->player->world;
	if (!world) return;

	Writer writer;
	writer.writeUInt8(0xc);
	for (auto player : world->players) {
		if (player->state != PlayerState::ALIVE) continue;
		writer.writeUInt16(player->id);
		float x = 128 * (world->border.w + player->viewArea.getX() - world->border.getX()) / world->border.w;
		x = x < 0 ? 0 : x;
		x = x > 255 ? 255 : x;
		float y = 128 * (world->border.h + player->viewArea.getY() - world->border.getY()) / world->border.h;
		y = y < 0 ? 0 : y;
		y = y > 255 ? 255 : y;
		writer.writeUInt8(x);
		writer.writeUInt8(y);
	}
	if (writer.offset() > 1) {
		writer.writeUInt16(0);
		printf("[SERVER LOG] Sending OpCode: 12 (Minimap Update)\n");
		send(writer.finalize());
	}
};

void writeAddOrUpdate(Writer& writer, vector<Cell*>& cells) {
	for (auto cell : cells) {
		unsigned char type = cell->getType();
		switch (type) {
			case PLAYER:
				type = cell->owner ? 1 : 5;
				break;
			case VIRUS:
				type = 2;
				break;
			case EJECTED_CELL:
				type = 3;
				break;
			case PELLET:
				type = 4;
				break;
		}
		writer.writeUInt8(type);
		if (type == 1)
			writer.writeUInt16(cell->owner->id);
		writer.writeUInt32(cell->id);
		writer.writeInt32(cell->getX());
		writer.writeInt32(cell->getY());
		writer.writeInt16(cell->getSize());
	}
}

void ProtocolVanis::onVisibleCellUpdate(vector<Cell*>& add, vector<Cell*>& upd, vector<Cell*>& eat, vector<Cell*>& del) {
	Writer writer;
	writer.writeUInt8(10);
	writeAddOrUpdate(writer, add);
	writeAddOrUpdate(writer, upd);
	writer.writeUInt8(0);
	for (auto cell : del)
		writer.writeUInt32(cell->id);
	writer.writeUInt32(0);
	for (auto cell : eat) {
		writer.writeUInt32(cell->id);
		writer.writeUInt32(cell->eatenBy->id);
	}
	writer.writeUInt32(0);
	printf("[SERVER LOG] Sending OpCode: 10 (Visible Cell Update)\n");
	send(writer.finalize());

	auto player = connection->player;
	if (!player) return;
	for (auto router : player->router->spectators) {

		if (router->type == RouterType::PLAYER
			&& router->hasPlayer
			&& router->player->state == PlayerState::SPEC
			&& router->spectateTarget == player->router)
			((Connection*) router)->protocol->send(writer.finalize());
	}
};

void ProtocolVanis::onVisibleCellThreadedUpdate() {
	/*
	auto player = connection->player;
	if (!player) return;
	Writer writer;
	writer.writeUInt8(10);
	// add and upd
	for (auto [id, data] : player->visibleCellData) {
		if (player->lastVisibleCellData.find(id) == player->lastVisibleCellData.cend()) {
			unsigned char type = data->type;
			switch (type) {
				case PLAYER:
					type = data->dead ? 5 : 1;
					break;
				case VIRUS:
					type = 2;
					break;
				case EJECTED_CELL:
					type = 3;
					break;
				case PELLET:
					type = 4;
					break;
			}
			writer.writeUInt8(type);
			if (type == 1)
				writer.writeUInt16(data->pid);
			writer.writeUInt32(data->id);
			writer.writeInt32(data->getX());
			writer.writeInt32(data->getY());
			writer.writeInt16(data->size);
		} else if (data->type != CellType::PELLET) {
			unsigned char type = data->type;
			switch (type) {
				case PLAYER:
					type = data->dead ? 5 : 1;
					break;
				case VIRUS:
					type = 2;
					break;
				case EJECTED_CELL:
					type = 3;
					break;
			}
			writer.writeUInt8(type);
			if (type == 1)
				writer.writeUInt16(data->pid);
			writer.writeUInt32(data->id);
			writer.writeInt32(data->getX());
			writer.writeInt32(data->getY());
			writer.writeInt16(data->size);
		}
	}
	writer.writeUInt8(0);
	for (auto [id, cell] : player->lastVisibleCellData)
		if (!cell->eatenById && player->visibleCellData.find(id) == player->lastVisibleCellData.cend())
			writer.writeUInt32(id);
	writer.writeUInt32(0);
	for (auto [id, cell] : player->lastVisibleCellData)
		if (cell->eatenById && player->visibleCellData.find(id) == player->lastVisibleCellData.cend()) {
			writer.writeUInt32(id);
			writer.writeUInt32(cell->eatenById);
		}
	writer.writeUInt32(0);

	auto buffer = writer.finalize();
	send(buffer);

	for (auto router : player->router->spectators) {

		if (router->type != RouterType::PLAYER 
			|| !router->hasPlayer
			|| router->player->state != PlayerState::SPEC 
			|| router->spectateTarget != player->router) continue;
		printf("Sending buffer from player#%u to player#%u\n", player->id, router->player->id);
		((Connection*)router)->send(buffer);
	} */
}

void ProtocolVanis::onStatsRequest() {
};

void ProtocolVanis::onTimingMatrix() {
	Writer writer;
	writer.writeUInt8(0x20);
	writer.writeBuffer(string_view((char*) &connection->listener->handle->timing, sizeof(TimingMatrix)));
	send(writer.finalize());
}

// New function to send dual player state to client
void ProtocolVanis::sendDualPlayerUpdate(Player* ownerPlayer) {
	if (!ownerPlayer || !connection || ownerPlayer != connection->player) {
		Logger::error("[ProtocolVanis] Attempted to sendDualPlayerUpdate for invalid player or connection state.");
		return;
	}

	unsigned short dualPid = 0;
	if (ownerPlayer->m_dualPlayer) {
		dualPid = ownerPlayer->m_dualPlayer->id;
	}

	unsigned short activePid = 0;
	Player* activePlayer = ownerPlayer->getActiveControlledPlayer();
	if (activePlayer) {
		activePid = activePlayer->id;
	}

	Writer writer;
	writer.writeUInt8(24); // Opcode for DualPlayerUpdate
	writer.writeUInt16(dualPid); 
	writer.writeUInt16(activePid);

	printf("[SERVER LOG] Sending OpCode: 24 (DualPlayerUpdate) to player %u (DualPID: %u, ActivePID: %u)\n", ownerPlayer->id, dualPid, activePid);
	send(writer.finalize());
}