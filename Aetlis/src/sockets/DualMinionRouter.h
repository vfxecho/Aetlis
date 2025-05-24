#pragma once
#include "./Router.h" // Assuming Router.h is in the same directory or adjust path

// Forward declarations
class World;
class PlayerCell;
class Listener; // Already in Router.h but good practice if used directly

class DualMinionRouter : public Router {
public:
    DualMinionRouter(Listener* listener, World* world);
    ~DualMinionRouter() override;

    // Router overrides
    bool isExternal() override { return false; } // Not an external connection
    void onWorldSet() override;                 // Called when player's world is set
    void onWorldReset() override;               // Called on world reset
    void onNewOwnedCell(PlayerCell*) override;  // Called when a new cell is added (likely no-op for passive minion)
    void close() override;                      // Cleanup logic
    bool shouldClose() override;                // Determines if the router and its player should be removed
    void update() override;                     // Main update, will be minimal as inputs are set externally
    bool isThreaded() override { return false; } // Assuming not threaded for simplicity
    void postUpdate() override;                 // Post-update logic (likely no-op)
    void onDead() override;                     // Called when the associated player dies

private:
    World* m_world; // Store world reference if needed for player interactions
}; 