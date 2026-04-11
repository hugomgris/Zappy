import re

with open("/home/jareste/hugo/zappy/client_cpp/srcs/app/AI.cpp", "r") as f:
    text = f.read()

# 1. Remove double onResponse call
text = text.replace('\t\t\t\tLogger::info("AI: Command succeeded: " + msg.cmd);\n\t\t\t\t_state.onResponse(msg);', 
                    '\t\t\t\tLogger::info("AI: Command succeeded: " + msg.cmd);')

# 2. Fix requestVision
old_vision = r'void AI::requestVision\(\) \{\s*_lastVoirTime = std::chrono::duration_cast<std::chrono::milliseconds>\(\s*std::chrono::steady_clock::now\(\)\.time_since_epoch\(\)\s*\)\.count\(\);\s*_sender\.sendVoir\(\);\s*\}'
new_vision = '''void AI::requestVision() {
\t\t_lastVoirTime = std::chrono::duration_cast<std::chrono::milliseconds>(
\t\t\tstd::chrono::steady_clock::now().time_since_epoch()
\t\t).count();
\t\t_sender.expectResponse("voir", [this](const ServerMessage& msg) {
\t\t\t// don't call onCommandComplete, vision doesn't consume an action queue slot here usually
\t\t});
\t\t_sender.sendVoir();
\t}'''
text = re.sub(old_vision, new_vision, text)

# 3. Fix requestInventory
old_inv = r'void AI::requestInventory\(\) \{\s*_lastInventaireTime = std::chrono::duration_cast<std::chrono::milliseconds>\(\s*std::chrono::steady_clock::now\(\)\.time_since_epoch\(\)\s*\)\.count\(\);\s*_sender\.sendInventaire\(\);\s*\}'
new_inv = '''void AI::requestInventory() {
\t\t_lastInventaireTime = std::chrono::duration_cast<std::chrono::milliseconds>(
\t\t\tstd::chrono::steady_clock::now().time_since_epoch()
\t\t).count();
\t\t_sender.expectResponse("inventaire", [this](const ServerMessage& msg) {});
\t\t_sender.sendInventaire();
\t}'''
text = re.sub(old_inv, new_inv, text)

with open("/home/jareste/hugo/zappy/client_cpp/srcs/app/AI.cpp", "w") as f:
    f.write(text)
