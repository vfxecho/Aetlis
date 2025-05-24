#define _HAS_STD_BYTE 0
#define _HAS_STD_BOOLEAN 0

#include <iostream>

#include "../ServerHandle.h"

#include "../protocols/ProtocolModern.h"
#include "../protocols/Protocol6.h"
#include "../protocols/ProtocolVanis.h"
#include "../gamemodes/FFA.h"

void registerGamemodes(ServerHandle* handle) {
	auto ffa = new FFA(handle);
	handle->gamemodes->registerGamemode(ffa);
}

void registerProtocols(ServerHandle* handle) {
	Protocol* ptc = new ProtocolModern(nullptr);
	handle->protocols->registerProtocol(ptc);
	ptc = new Protocol6(nullptr);
	handle->protocols->registerProtocol(ptc);
	ptc = new ProtocolVanis(nullptr);
	handle->protocols->registerProtocol(ptc);
}

bool exited = false;
void registerCommands(ServerHandle* handle) {

	Command<ServerHandle*> startCommand("start", "start the handle", "",
		[](ServerHandle* handle, auto context, vector<string>& args) {
		if (!handle->start()) Logger::info("Handle already running");
	});
	handle->commands.registerCommand(startCommand);

	Command<ServerHandle*> stopCommand("stop", "stop the handle", "",
		[](ServerHandle* handle, auto context, vector<string>& args) {
		if (!handle->stop()) Logger::info("Handle not running");
	});
	handle->commands.registerCommand(stopCommand);

	Command<ServerHandle*> exitCommand("exit", "stop the handle and close the command stream", "",
		[](ServerHandle* handle, auto context, vector<string>& args) {
		handle->exiting = true;
		handle->stop();
		exited = true;
		Logger::info("Press any key to exit OgarCpp");
	});
	handle->commands.registerCommand(exitCommand);

	Command<ServerHandle*> reloadCommand("reload", "reload the settings from local game.cfg", "",
		[](ServerHandle* handle, auto context, vector<string>& args) {
		handle->loadSettings();
	});
	handle->commands.registerCommand(reloadCommand);

	Command<ServerHandle*> saveCommand("save", "save the current settings to game.cfg", "",
		[](ServerHandle* handle, auto context, vector<string>& args) {
		saveConfig();
	});
	handle->commands.registerCommand(saveCommand);

	Command<ServerHandle*> benchCommand("bench", "print out how long each part of function takes to execute", "",
		[](ServerHandle* handle, auto context, vector<string>& args) {
		handle->bench = true;
	});
	handle->commands.registerCommand(benchCommand);

	Command<ServerHandle*> monitorStartCommand("mstart", "monitor load and cell count", "",
		[](ServerHandle* handle, auto context, vector<string>& args) {
		handle->bytesSent = 0;
		handle->ticker.every(20, [handle] {
			if (handle->worlds.size()) {
				printf("Load: %2.2f%% ", handle->worlds.begin()->second->stats.loadTime);
				printf("Bandwidth: %2.2fkb/s", handle->bytesSent / 1024.0f);
				printf("cells: %lu\n",   handle->worlds.begin()->second->cells.size());
				handle->bytesSent = 0;
			}
		});
	});
	handle->commands.registerCommand(monitorStartCommand);

	Command<ServerHandle*> monitorStopCommand("mstop", "stop the monitor", "",
		[](ServerHandle* handle, auto context, vector<string>& args) {
		handle->ticker.clearInterval();
	});
	handle->commands.registerCommand(monitorStopCommand);

	Command<ServerHandle*> ejectCommand("meject", "", "",
		[](ServerHandle* handle, auto context, vector<string>& args) {
		handle->ticker.every(10, [handle] {
			if (handle->worlds.size()) {
				printf("Ejected count: %u\n", handle->worlds.begin()->second->ejectCount);
				handle->worlds.begin()->second->ejectCount = 0;
			}
		});
	});
	handle->commands.registerCommand(ejectCommand);
}

void promptInput(ServerHandle& handle) {
	Logger::verbose("CLI reader open");
	string input;
	while (!exited) {
		std::cout << "> ";
		if (std::cin) std::getline(std::cin, input);
		else break;
		input = trim(input);
		if (!input.length()) continue;
		handle.commands.execute(nullptr, input);
		std::this_thread::sleep_for(milliseconds{ 150 });
	}
	Logger::verbose("CLI reader closed");
}

int main() {
	
	ServerHandle handle;

	registerGamemodes(&handle);
	registerProtocols(&handle);
	registerCommands(&handle);

	handle.start();

	std::this_thread::sleep_for(seconds{ 1 });

	promptInput(handle);
	return EXIT_SUCCESS;
}