#include "app/Client.hpp"
#include "helpers/Logger.hpp"
#include <iostream>
#include <csignal>
#include <cstring>
#include <getopt.h>

static zappy::Client* g_client = nullptr;

void signalHandler(int sig) {
	if (sig == SIGINT || sig == SIGTERM) {
		std::cout << std::endl << "Shutting down..." << std::endl;
		if (g_client) {
			g_client->stop();
		}
	}
}

void printUsage(const char* progName) {
	std::cout << "Usage: " << progName << " [options] <host> <port> <team_name>\n"
			<< "Options:\n"
			<< "  --fork            Enable automatic forking\n"
			<< "  --target-level N  Target level to reach (default: 8)\n"
			<< "  --max-forks N     Maximum number of forks (default: 5)\n"
			<< "  --debug           Enable debug logging\n"
			<< "  --help            Show this help\n";
}

int main(int argc, char** argv) {
	// FIXED: Ignore SIGPIPE to prevent crash on broken pipe
	signal(SIGPIPE, SIG_IGN);

	// command line parsing
	bool forkEnabled = false;
	int targetLevel = 8;
	int maxForks = 5;
	bool debugMode = false;

	static struct option long_options[] = {
		{"fork", no_argument, 0, 'f'},
		{"target-level", required_argument, 0, 't'},
		{"max-forks", required_argument, 0, 'm'},
		{"debug", no_argument, 0, 'd'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "ft:m:dh", long_options, nullptr)) != -1) {
		switch (opt) {
			case 'f': forkEnabled = true; break;
			case 't': targetLevel = std::stoi(optarg); break;
			case 'm': maxForks = std::stoi(optarg); break;
			case 'd': debugMode = true; break;
			case 'h': printUsage(argv[0]); return 0;
			default: printUsage(argv[0]); return 1;
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
	Logger::info("Target Level: " + std::to_string(targetLevel));
	Logger::info("Fork enabled: " + std::string(forkEnabled ? "yes" : "no"));
	
	// Create and configure client
	zappy::Client client(host, port, teamName);
	g_client = &client;

	client.setForkEnabled(forkEnabled);
	client.setTargetLevel(targetLevel);
	client.setMaxForks(maxForks);

	// add msg callback for monitoring
	client.onMessage([](const zappy::ServerMessage& msg) {
		if (msg.type == zappy::ServerMessageType::Event && msg.isLevelUp()) {
			Logger::info("LEVEL UP!!");
		}
	});

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
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	Logger::info("Client exited. Final level: " + std::to_string(client.getState().getLevel()));
	
	return 0;
}