#include "app/Client.hpp"
#include "helpers/Logger.hpp"
#include <iostream>
#include <csignal>
#include <cstring>
#include <getopt.h>

static zappy::Client* g_client = nullptr;
static volatile sig_atomic_t g_stop_requested = 0;

void signalHandler(int sig) {
	if (sig == SIGINT || sig == SIGTERM) {
		g_stop_requested = 1;
	}
}

void printUsage(const char* progName) {
	std::cout << "Usage: " << progName << " [options] <host> <port> <team_name>\n"
			<< "Options:\n"
			<< "  --no-fork         Disable automatic forking (default: enabled)\n"
			<< "  --debug           Enable debug logging\n"
			<< "  --help            Show this help\n";
}

int main(int argc, char** argv) {
	// FIXED: Ignore SIGPIPE to prevent crash on broken pipe
	signal(SIGPIPE, SIG_IGN);

	// command line parsing
	bool forkEnabled = true;  // forking is on by default
	bool debugMode = false;

	static struct option long_options[] = {
		{"no-fork", no_argument, 0, 'F'},
		{"debug",   no_argument, 0, 'd'},
		{"help",    no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "Fdh", long_options, nullptr)) != -1) {
		switch (opt) {
			case 'F': forkEnabled = false; break;
			case 'd': debugMode = true; break;
			case 'h': printUsage(argv[0]); return 0;
			default:  printUsage(argv[0]); return 1;
		}
	}

	if (optind + 3 > argc) {
		std::cerr << "Error: Missing required arguments" << std::endl;
		printUsage(argv[0]);
		return 1;
	}

	std::string host = argv[optind];
	int port = std::stoi(argv[optind + 1]);
	std::string teamName = argv[optind + 2];

	// setup logging
	Logger::setLevel(debugMode ? LogLevel::Debug : LogLevel::Info);

	// setup signal handling
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	Logger::info("Zappy Client starting...");
	Logger::info("Team: " + teamName);
	Logger::info("Fork enabled: " + std::string(forkEnabled ? "yes" : "no"));
	
	// Create and configure client
	zappy::Client client(host, port, teamName);
	g_client = &client;

	client.setForkEnabled(forkEnabled);

	// Temporarily disable external message callback wiring.
	// Core AI/world logging remains active and this avoids callback-related
	// crashes observed immediately after bienvenue on this rollback commit.

	// connect
	zappy::Result res = client.connect();
	if (!res.ok()) {
		Logger::error("Failed to connect: " + res.message);
		return 1;
	}

	Logger::info("Connected! Starting main loop...");

	// run client
	res = client.run();
	if (!res.ok()) {
		Logger::error("Client error: " + res.message);
		return 1;
	}

	// wait for ctrl+c
	while (client.isRunning()) {
		if (g_stop_requested) {
			std::cout << std::endl << "Shutting down..." << std::endl;
			client.stop();
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	Logger::info("Client exited. Final level: " + std::to_string(client.getState().getLevel()));
	
	return 0;
}