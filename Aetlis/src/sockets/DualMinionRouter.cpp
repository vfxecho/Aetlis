#include "DualMinionRouter.h"
#include "../worlds/Player.h" // For Player class access
#include "../worlds/World.h"   // For World class access
#include "../ServerHandle.h" // For Listener -> ServerHandle -> Logger access
#include "../protocols/ProtocolVanis.h" // Added for ProtocolVanis type
#include "Listener.h"        // For Listener access
#include "Connection.h"      // Added for Connection type for onDead

DualMinionRouter::DualMinionRouter(Listener* listener, World* world)
    : Router(listener), m_world(world) {
    this->type = RouterType::PLAYER_BOT; // Or PLAYER_DUAL_MINION if we add it later
    Logger::debug("DualMinionRouter created.");
}

DualMinionRouter::~DualMinionRouter() {
    // Ensure player is cleaned up if this router is deleted directly
    // Router base class destructor should handle player if this->player is set
    if (this->player) {
         // destroyPlayer(); // This is in base Router, let base destructor handle it or call it if appropriate
    }
    Logger::debug("DualMinionRouter destroyed.");
}

void DualMinionRouter::onWorldSet() {
    // Called when the player associated with this router has its world set.
    // If player is already created and has this router, player->world should be m_world.
    if (this->player && this->player->world != m_world) {
        // This might indicate a logic issue if player's world differs from router's initial world
        Logger::warn("DualMinionRouter: Player world mismatch onWorldSet.");
    }
}

void DualMinionRouter::onWorldReset() {
    // If there are any specific states this router holds that need reset, do it here.
    // For a passive minion, probably not much.
}

void DualMinionRouter::onNewOwnedCell(PlayerCell* cell) {
    // Likely a no-op for a passively controlled minion.
    // The owner player will manage cells.
}

void DualMinionRouter::close() {
    // This function is called to initiate the closing of the router and its associated player.
    this->disconnected = true;
    this->disconnectedTick = (listener && listener->handle) ? listener->handle->tick : 0;
    
    Logger::info("DualMinionRouter close() called for player ID: " + (this->player ? std::to_string(this->player->id) : "N/A"));

    // The actual deletion of player and router might be handled by a cleanup process
    // in ServerHandle that checks router->shouldClose().
}

bool DualMinionRouter::shouldClose() {
    // Conditions under which this router (and its player) should be removed:
    // 1. Explicitly disconnected.
    // 2. Associated player doesn't exist or its world is gone.
    // 3. Owner player (if this is a dual minion) no longer exists or has no dual player set to this.
    if (this->disconnected) {
        return true;
    }
    if (!this->hasPlayer || !this->player || !this->player->exist() || !this->player->hasWorld) {
        return true;
    }
    if (this->player->m_playerType == PlayerType::DUAL_MINION) {
        if (!this->player->m_ownerPlayer || 
            !this->player->m_ownerPlayer->exist() || 
            this->player->m_ownerPlayer->m_dualPlayer != this->player) {
            // Owner is gone or has disowned this dual minion
            return true;
        }
    }
    return false;
}

void DualMinionRouter::update() {
    // Main update loop for this router.
    // For a DualMinionRouter, inputs (mouse, split, eject) are set externally by the owner player's actions
    // when this minion is the activeControlledPlayer.
    // So, this update() might not need to do much in terms of AI or input generation.
    // It could be used for very basic periodic checks if needed.

    // If player exists, call its input processing methods if they are not called elsewhere for bots
    // This depends on how OgarCpp structures its main loop for bots vs players.
    // For now, assume these are handled by Player::update or World::updatePlayers
    /* if (this->hasPlayer && this->player) {
        onSpawnRequest();    // Process spawn requests (e.g., if bot died and wants to respawn)
        onSpectateRequest(); // Process spectate requests
        attemptSplit();      // Process split attempts
        attemptEject();      // Process eject attempts
    } */
}

void DualMinionRouter::postUpdate() {
    // Post-update logic, if any. Likely no-op.
}

void DualMinionRouter::onDead() {
    // Called when the associated player is officially declared dead.
    if (this->player) {
        Logger::info("DualMinionRouter: Player " + std::to_string(this->player->id) + " (Dual Minion) died.");
    }
    // The owner player might want to know this to clear its m_dualPlayer pointer or re-create it.
    // This notification should probably go from Player (dual) -> Player (owner) -> then owner might call close() on this router.
    // For now, this router will be cleaned up if shouldClose() becomes true.
    if (this->player && this->player->m_playerType == PlayerType::DUAL_MINION && this->player->m_ownerPlayer) {
        // If this dual minion dies, its owner should probably nullify its m_dualPlayer pointer.
        // And potentially send an update to the client that the dual is gone.
        Player* owner = this->player->m_ownerPlayer;
        if (owner->m_dualPlayer == this->player) {
            owner->m_dualPlayer = nullptr; // Owner disowns the dead dual
            // Owner should send an update to the client
            // Corrected access to protocol, similar to Player.cpp fixes
            if (owner->router && owner->router->type == RouterType::PLAYER) {
                Connection* conn = static_cast<Connection*>(owner->router);
                if (conn->protocol) {
                    ProtocolVanis* vanisProtocol = dynamic_cast<ProtocolVanis*>(conn->protocol);
                    if (vanisProtocol) {
                        vanisProtocol->sendDualPlayerUpdate(owner);
                    } else {
                        Logger::warn("DualMinionRouter::onDead: Owner router protocol is not ProtocolVanis for player " + std::to_string(owner->id));
                    }
                }
            } else {
                Logger::warn("DualMinionRouter::onDead: Owner router is not PLAYER type for player " + std::to_string(owner->id));
            }
        }
    }
} 