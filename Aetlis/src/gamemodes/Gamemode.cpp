#include "Gamemode.h"
#include "../ServerHandle.h"
#include "../worlds/World.h"
#include "../cells/Cell.h"
#include <cmath> // pow fonksiyonu için

void Gamemode::onPlayerPressQ(Player* player) {
	player->updateState(PlayerState::ROAM);
}

void Gamemode::onPlayerSplit(Player* player) {
	if (!player->hasWorld) return;
	player->world->splitPlayer(player);
}

void Gamemode::onPlayerEject(Player* player) {
	if (!player->hasWorld) return;
	player->world->ejectFromPlayer(player);
}

float Gamemode::getDecayMult(Cell* cell) {
	float baseMult = cell->world->handle->runtime.playerDecayMult;
	
	// Check if this is a player cell and its mass exceeds 250,000
	if (cell->getType() == PLAYER) {
		float mass = cell->getMass();
		if (mass > 250000) {
			// Çok daha agresif bir decay uygula
			// 250k üzerinde exponential (üstel) artış uygulayalım
			float excessRatio = mass / 250000.0f;
			// Üstel artış formülü - çok daha hızlı decay
			float multiplier = pow(excessRatio, 3.0f) * 10.0f; // Küp fonksiyonu ve 10x çarpan
			return baseMult * multiplier;
		}
	}
	
	return baseMult;
}