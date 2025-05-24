#pragma once
#include <list>
#include <regex>
#include <atomic>

using std::atomic;
using std::string;
using std::list;

class Listener;
class Player;
class PlayerCell;

static std::regex nameSkinRegex{ "<(.*)>(.*)" };

enum class RouterType {
	NONE, PLAYER, PLAYER_BOT, MINION
};

class Router;

class Router {
public:
	Listener* listener;

	bool disconnected = false;
	unsigned long disconnectedTick = 0;
	RouterType type = RouterType::NONE;

	atomic<bool> requestSpawning = false;
	string spawningName = "";
	string spawningSkin = "";
	string spawningTag  = "";

	atomic<bool> admin = false;
	atomic<bool> busy = false;
	atomic<float> mouseX = 0;
	atomic<float> mouseY = 0;
	atomic<bool> requestingSpectate = false;
	atomic<unsigned int> spectatePID = 0;
	Router* spectateTarget = nullptr;
	list<Router*> spectators;
	atomic<bool> isPressingQ = false;
	atomic<bool> hasPressedQ = false;
	atomic<bool> ejectMacro  = false;
	atomic<bool> linelocked  = false;
	atomic<unsigned short> splitAttempts = 0;
	atomic<unsigned short> ejectAttempts = 0;

	unsigned long ejectTick;
	atomic<bool> hasPlayer = false;
	Player* player = nullptr;
	
	Router(Listener* listener);
	~Router();
	virtual bool isExternal() = 0;
	void createPlayer();
	void destroyPlayer();
	virtual void onWorldSet() = 0;
	virtual void onWorldReset() = 0;
	virtual void onNewOwnedCell(PlayerCell*) = 0;
	void onQPress();
	void onSpawnRequest();
	void onSpectateRequest();
	void attemptSplit();
	void attemptEject();
	virtual void close() = 0;
	virtual bool shouldClose() = 0;
	virtual void update() = 0;
	virtual bool isThreaded() = 0;
	virtual void postUpdate() = 0;
	virtual void onDead() = 0;
};