#pragma once

#include <nlohmann/json.hpp>
#include <iomanip>
#include <fstream>
#include "primitives/Logger.h"

using Json = nlohmann::json;
using std::string;
using std::ifstream;
using std::ofstream;

inline Json GAME_CONFIG = R"(
{
    "serveWeb": false,
    "webPort": 80,
    "webRoot": "",
    "spawnProtection": 40,
    "restartMulti": 0.75,
    "killOversize": false,
    "enableSSL": false,
    "key_file_name": "",
	"cert_file_name": "",
	"passphrase": "",
	"dh_params_file_name": "",
	"ca_file_name": "",
    "listenerAcceptedOriginRegex" : ".*",
    "listenerMaxConnections" : 100,
    "listenerMaxClientDormancy" : 60000,
    "listenerMaxConnectionsPerIP" : 1,
    "listenerThreads" : 6,
    "listeningPort" : 443,
    "serverFrequency" : 25,
    "serverName" : "An unnamed server",
    "serverGameMode" : "FFA",
    "respawnEnabled" : true,
    "chatEnabled" : true,
    "chatFilteredPhrases" : [] ,
    "chatCooldown" : 1000,
    "socketsThreads" : 6,
    "physicsThreads" : 6,
    "worldMapX" : 0,
    "worldMapY" : 0,
    "worldMapW" : 7071,
    "worldMapH" : 7071,
    "worldFinderMaxLevel" : 16,
    "worldFinderMaxItems" : 16,
    "worldFinderMaxSearch" : 0,
    "worldSafeSpawnTries" : 128,
    "worldSafeSpawnFromEjectedChance" : 0.8,
    "worldPlayerDisposeDelay" : 100,
    "worldEatMult" : 1.140175425099138,
    "worldEatOverlapDiv" : 3,
    "worldPlayerBotsPerWorld" : 0,
    "worldPlayerBotNames" : [] ,
    "worldPlayerBotSkins" : [] ,
    "worldMinionsPerPlayer" : 0,
    "worldMaxPlayers" : 50,
    "worldMinCount" : 0,
    "worldMaxCount" : 2,
    "matchmakerNeedsQueuing" : false,
    "matchmakerBulkSize" : 1,
    "minionName" : "Minion",
    "minionSpawnSize" : 32,
    "minionEnableERTPControls" : false,
    "minionEnableQBasedControl" : true,
    "pelletMinSize" : 10,
    "pelletMaxSize" : 20,
    "pelletGrowTicks" : 1500,
    "pelletCount" : 100,
    "virusMinCount" : 30,
    "virusMaxCount" : 90,
    "virusSize" : 100,
    "virusFeedTimes" : 7,
    "virusPushing" : false,
    "virusSplitBoost" : 780,
    "virusPushBoost" : 120,
    "virusMonotonePops" : false,
    "ejectedSize" : 38,
    "ejectingLoss" : 43,
    "ejectDispersion" : 0.3,
    "ejectedCellBoost" : 780,
    "mothercellSize" : 149,
    "mothercellCount" : 0,
    "mothercellPassiveSpawnChance" : 0.05,
    "mothercellActiveSpawnSpeed" : 1,
    "mothercellPelletBoost" : 90,
    "mothercellMaxPellets" : 96,
    "mothercellMaxSize" : 65535,
    "playerRoamSpeed" : 32,
    "playerRoamViewScale" : 0.4,
    "playerViewScaleMult" : 1,
    "playerMinViewScale" : 0.01,
    "playerMaxNameLength" : 16,
    "playerAllowSkinInName" : true,
    "playerMinSize" : 32,
    "playerSpawnSize" : 32,
    "playerMaxSize" : 1500,
    "playerMinSplitSize" : 60,
    "playerMinEjectSize" : 60,
    "playerSplitCap" : 255,
    "playerEjectDelay" : 2,
    "playerMaxCells" : 16,
    "playerMoveMult" : 1,
    "playerSplitSizeDiv" : 1.414213562373095,
    "playerSplitDistance" : 40,
    "playerSplitBoost" : 780,
    "playerNoCollideDelay" : 14,
    "playerNoMergeDelay" : 0.5,
    "playerMergeNewVersion" : false,
    "playerMergeTime" : 30,
    "playerMergeTimeIncrease" : 0.02,
    "playerDecayMult" : 0.001
}
)"_json;

static const char* configPath = "config.json";

inline void loadConfig() {
    ifstream i(configPath);
    if (i.is_open() && i.good()) {
        Logger::info(string("Reading config from ") + string(configPath));
        i >> GAME_CONFIG;
    } else {
        ofstream o(configPath);
        Logger::info(string("Writing default config to ") + string(configPath));
        o << std::setw(4) << GAME_CONFIG << std::endl;
        o.close();
    }
    i.close();
}

inline void saveConfig() {
    ofstream o(configPath);
    Logger::info(string("Writing config to ") + string(configPath));
    o << std::setw(4) << GAME_CONFIG << std::endl;
    o.close();
}