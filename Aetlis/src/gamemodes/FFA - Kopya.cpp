#include <algorithm>
#include "FFA.h"
#include "../misc/Misc.h"
#include "../worlds/Player.h"
#include "../worlds/World.h"
#include "../sockets/Connection.h"
#include "../ServerHandle.h"

void FFA::onPlayerSpawnRequest(Player* player, string name, string skin) {
	// Check for spawn cooldown
	if (player->spawnCooldownActive) {
		if (handle->tick < player->spawnCooldownEndTick) {
			return; // Cooldown active, ignore request
		} else {
			player->spawnCooldownActive = false; // Cooldown finished
		}
	}

	if (!player->hasWorld) return;
	if (player->state == PlayerState::ALIVE && handle->runtime.respawnEnabled) {
		player->world->killPlayer(player);
	}

	float size = 0.0f;
	
	switch (player->router->type) {
		case RouterType::PLAYER:
			size = handle->runtime.playerSpawnSize;
			break;
		case RouterType::PLAYER_BOT:
			size = handle->runtime.botSpawnSize;
			break;
		case RouterType::MINION:
		default:
			size = handle->runtime.minionSpawnSize;
			break;
	}

	bool failed = false;
	auto spawnResult = player->world->getPlayerSpawn(size, failed);
	if (failed) {
		player->router->requestSpawning = true;
	} else {
		unsigned int color = spawnResult.color ? spawnResult.color : randomColor();
		player->cellName = player->chatName = player->leaderboardName = name;
		player->cellSkin = skin;
		player->chatColor = player->cellColor = color;
		player->world->spawnPlayer(player, spawnResult.pos, size);
	}
}

void FFA::compileLeaderboard(World* world) {
	leaderboard.clear();
	for (auto player : world->players)
		if (player->score > 0)
			leaderboard.push_back(player);
	std::sort(leaderboard.begin(), leaderboard.end(), [](Player* a, Player* b) {
		return b->score < a->score;
	});
}

void FFA::sendLeaderboard(Connection* connection) {
	if (!connection->hasPlayer) return;
	auto player = connection->player;
	if (!player->hasWorld) return;
	if (player->world->frozen) return;
	vector<LBEntry*> lbData;
	FFAEntry* lbSelfData = nullptr;
	int position = 1;
	for (auto player : leaderboard) {
		auto entry = new FFAEntry();
		entry->pid = player->id;
		entry->position = position++;
		entry->name = player->leaderboardName;
		entry->cellId = player->ownedCells.size() ? player->ownedCells.front()->id : 0;
		if (connection->player == player) {
			entry->highlighted = true;
			lbSelfData = entry;
		}
		lbData.push_back(entry);
	}
	connection->protocol->onLeaderboardUpdate(LBType::FFA, lbData, lbSelfData);
	for (auto entry : lbData) delete entry;
}