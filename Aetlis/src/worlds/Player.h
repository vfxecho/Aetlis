#pragma once
#include <string>
#include <unordered_map>
#include <list>
#include "../primitives/Rect.h"
#include "../cells/Cell.h"

using std::string;
using std::unordered_map;
using std::list;

class ServerHandle;
class PlayerCell;
class Router;
class World;

// Define PlayerType enum
enum class PlayerType : unsigned char {
	REGULAR,
	DUAL_MINION
};

enum class PlayerState : unsigned char { 
	DEAD, ALIVE, SPEC, ROAM 
};

class Player {
public:
	ServerHandle* handle;
	unsigned int id;
	Router* router;
	string leaderboardName = "";
	string cellName = "";
	string chatName = "Spectator";
	string cellSkin = "";
	unsigned int cellColor = 0x7F7F7F;
	unsigned int chatColor = 0x7F7F7F;
	PlayerState state = PlayerState::DEAD;
	bool hasWorld = false;
	bool justPopped = false;
	World* world = nullptr;
	short team = -1;
	float score = 0;

	PlayerType m_playerType = PlayerType::REGULAR; // To identify if this is a regular player or a dual minion
	Player* m_dualPlayer = nullptr;                // For a regular player, points to their dual minion
	Player* m_ownerPlayer = nullptr;               // For a DUAL_MINION, points to their owner
	bool m_isDualActive = false;                  // For a regular player (owner), true if the dual minion is currently controlled

	Point lastDeathPosition = {0.0f, 0.0f};
	bool justDied = false;

	// Line lock state
	std::atomic<bool> isLineLocked = false;
	float lineEqA = 0.0f;
	float lineEqB = 0.0f;
	float lineEqC = 0.0f;
	float lineEqDenomInv = 0.0f; // Precalculated 1 / (a^2 + b^2)

	unsigned short killCount = 0;
	float maxScore = 0;
	unsigned long joinTick = 0;

	// Spawn cooldown state
	bool spawnCooldownActive = false;
	unsigned long spawnCooldownEndTick = 0;
	
	// Linelock projection state
	// std::atomic<bool> isLineLocked = false;      // Opcode 15: Generic linelock state (THIS IS THE DUPLICATE TO REMOVE)
	unsigned long projectionActiveUntilTick = 0; // Opcode 15: Tick until which generic projection is allowed (if special is not active)
	bool needsProjectionReactivation = false;    // Opcode 15: If generic linelock needs re-toggle (if special is not active)
	bool specialLineSplitLockActive = false;     // Opcode 18: State of the special, persistent linelock for linesplit maneuver
	unsigned long specialLineSplitLockCooldownEndTick = 0; // Opcode 18: Cooldown for *activating* special lock
	
	// For sequential buffering
	list<PlayerCell*> ownedCells;
	unordered_map<unsigned int, Cell*> visibleCells;
	unordered_map<unsigned int, Cell*> lastVisibleCells;

	// For threaded buffering
	list<CellData*> ownedCellData;
	unordered_map<unsigned int, CellData*> visibleCellData;
	unordered_map<unsigned int, CellData*> lastVisibleCellData;
	QuadTree* lockedFinder = nullptr;
	
	ViewArea viewArea = ViewArea(0, 0, 1920 / 2, 1080 / 2, 1);

	Player(ServerHandle* handle, unsigned int id, Router* router);

	~Player();

	void updateState(PlayerState state);
	void updateViewArea();
	void updateVisibleCells(bool threaded = false);
	bool exist();

	// Dual player methods
	void createDualPlayer();                      // Creates and links the m_dualPlayer
	void setDualActive(bool active);              // Sets the m_isDualActive flag (for owner)
	Player* getActiveControlledPlayer();          // Returns 'this' or 'm_dualPlayer' based on m_isDualActive
	bool isDualMode() const;                      // Returns true if this player has a dual minion
	bool isOwnedByDualMaster() const;             // Returns true if this player is a dual minion
};