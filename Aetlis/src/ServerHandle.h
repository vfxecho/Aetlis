#pragma once

#include <chrono>
#include <map>
#include "Settings.h"
#include "primitives/Logger.h"
#include "gamemodes/Gamemode.h"
#include "commands/CommandList.h"
#include "misc/Ticker.h"
#include "misc/Stopwatch.h"
#include "sockets/Router.h"
#include "sockets/Listener.h"
#include "worlds/World.h"
#include "worlds/MatchMaker.h"
#include "protocols/ProtocolStore.h"
#include "gamemodes/GamemodeList.h"
#include "gamemodes/Gamemode.h"

class Gamemode;
class GamemodeList;

using namespace std::chrono;

struct RuntimeSettings {
	int spawnProtection;
	float restartMulti;
	bool killOversize;
	vector<string> botNames;
	vector<string> botSkins;
	int botSpawnSize;
	string serverName;
	bool chatEnabled;
	int worldMaxPlayers;
	int worldMinCount;
	int worldMaxCount;
	int physicsThreads;
	bool respawnEnabled;
	int listenerMaxConnections;
	int listenerMaxConnectionsPerIP;
	int chatCooldown;
	int matchmakerBulkSize;
	bool minionEnableQBasedControl;
	bool matchmakerNeedsQueuing;
	float minionSpawnSize;
	float worldEatMult;
	float worldEatOverlapDiv;
	int worldSafeSpawnTries;
	float worldSafeSpawnFromEjectedChance;
	int worldPlayerDisposeDelay;
	int pelletMinSize;
	int pelletMaxSize;
	int pelletGrowTicks;
	int pelletCount;
	int virusMinCount;
	int virusMaxCount;
	float virusSize;
	int virusFeedTimes;
	bool virusPushing;
	float virusSplitBoost;
	float virusPushBoost;
	bool virusMonotonePops;
	float ejectedSize;
	float ejectingLoss;
	float ejectDispersion;
	float ejectedCellBoost;
	float mothercellSize;
	int mothercellCount;
	float mothercellPassiveSpawnChance;
	float mothercellActiveSpawnSpeed;
	float mothercellPelletBoost;
	int mothercellMaxPellets;
	float mothercellMaxSize;
	float worldRespawnNearDeathRadius;
	float playerRoamSpeed;
	float playerRoamViewScale;
	float playerViewScaleMult;
	float playerMinViewScale;
	int playerMaxNameLength;
	bool playerAllowSkinInName;
	float playerMinSize = 32.0;
	float playerSpawnSize = 32.0;
	float playerMaxSize = 1500.0;
	float playerMinSplitSize = 60.0;
	float playerMinEjectSize = 60.0;
	int playerSplitCap = 255;
	int playerEjectDelay = 2;
	int playerMaxCells = 16;

	float playerMoveMult = 1;
	float playerSplitSizeDiv = 1.414213562373095;
	float playerSplitDistance = 40;
	float playerSplitBoost = 780;
	float playerNoCollideDelay = 13;
	float playerNoMergeDelay = 15.0;
	bool playerMergeNewVersion = false;
	float playerMergeTime = 30;
	float playerMergeTimeIncrease = 0.02;
	float playerDecayMult = 0.001;
};

struct TimingMatrix {
	float physicsTotal = 0.0f;
	float routerTotal  = 0.0f;
	float totalCells = 0.0f;
	float bandwidth = 0.0f;
	float tickCells = 0.0f;
	float spawnCell = 0.0f;
	float insides = 0.0f;
	float boostCell = 0.0f;
	float updatePC = 0.0f;
	float sortCell = 0.0f;
	float quadTree = 0.0f;
	float queryOPs = 0.0f;
	float rgdCheck = 0.0f;
	float eatCheck = 0.0f;
	float viewarea = 0.0f;
};

class ServerHandle {
public:
	TimingMatrix timing;

	ProtocolStore* protocols;
	GamemodeList*  gamemodes;

	Gamemode* gamemode;
	RuntimeSettings runtime;

	CommandList<ServerHandle*> commands     = CommandList<ServerHandle*>(this);
	CommandList<ServerHandle*> chatCommands = CommandList<ServerHandle*>(this);

	bool exiting = false;
	bool bench = false;
	bool running = false;
	unsigned long tick = -1;
	int tickDelay = -1;
	int stepMult = -1;
	atomic<size_t> bytesSent = 0;

	float averageTickTime = 0.0;

	Ticker ticker;
	Stopwatch stopwatch;
	
	time_point<system_clock> startTime = system_clock::now();
	Listener listener = Listener(this);
	MatchMaker matchmaker = MatchMaker(this);

	std::map<unsigned int, World*>  worlds;
	std::map<unsigned int, Player*> players;

	ServerHandle();
	~ServerHandle();

	void loadSettings();
	
	int getSettingInt(const char* key);
	bool getSettingBool(const char* key);
	float getSettingFloat(const char* key);
	std::string getSettingString(const char* key);

	void onTick();
	bool start();
	bool stop();
	Player* createPlayer(Router* router);
	bool removePlayer(unsigned int id);
	World* createWorld();
	bool removeWorld(unsigned int id);

	string randomBotName();
	string randomBotSkin();
};