package com.example;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import java.util.Set;
import java.util.EnumSet;
import java.util.Map;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;

public class AI {
    private enum ElevationState {
        IDLE,
        GATHERING,
        WAITING_ALLIES,
        INCANTING,
        COOLDOWN
    }

    // private String teamName;
    private final Player player;
    private World world;
    private Set<Resource> targets = EnumSet.noneOf(Resource.class);
    private List<List<String>> curView = new ArrayList<>();
    // private int debugLevel = 1;
    private boolean inventaireChecked = false;
    private int elevationBroadcastCooldown = 0;
    private static final int ELEVATION_BROADCAST_COOLDOWN_TICKS = 4;
    private ElevationState elevationState = ElevationState.IDLE;
    private int elevationAttemptCooldown = 0;
    private static final int ELEVATION_ATTEMPT_COOLDOWN_TICKS = 6;

    public AI(Player player) {
        this.player = player;
    }

    /********** DECISION MAKING **********/

    public List<Command> decideNextMoves() {
        List<Command> commands = new ArrayList<>();
        tickCooldowns();

        updateElevationStateForReadiness();
        if (inventaireChecked && readyToElevate()) {
            return guardedElevationAttempt();
        }
        addRandomMove(commands);
        return commands;
    }

    public List<Command> decideNextMovesViewBased(List<List<String>> viewData) {
        this.curView = viewData;
        tickCooldowns();
        // List<Command> commands = new ArrayList<>();
        setTargets();

        // int tileIdx = findItemInView(Resource.NOURRITURE);
        // if (tileIdx != -1) {
        //     List<CommandType> moves = getMovesToTile(tileIdx);
        //     System.out.println("MOVES to nourriture: " + moves);
        //     for (CommandType move : moves) {
        //         commands.add(new Command(move));
        //     }
        //     commands.add(new Command(CommandType.PREND, Resource.NOURRITURE.getName()));
        // } else {
        //     commands.add(new Command(CommandType.AVANCE));
        // }

        // return decideNextMovesRandom();

        if (player.getLife() < 1500 || player.getNour() < 20) {
            elevationState = ElevationState.GATHERING;
            System.out.println("[Client "+ player.getId() + "] I AM GOING FOR FOOD");
            return searchForFood();
        }
        if (inventaireChecked && !readyToElevate()) {
            setInventaireChecked(false);
        }
        if (!readyToElevate()) {
            elevationState = ElevationState.GATHERING;
            System.out.println("[Client "+ player.getId() + "] I AM GOING FOR TARGET STONE");
            return searchForTarget();
        } else if (!inventaireChecked) {
            elevationState = ElevationState.GATHERING;
            System.out.println("[Client "+ player.getId() + "] I AM GOING TO ELEVATE to level " + (player.getLevel() + 1) + ", CHECKING INVENTAIRE");
            return checkInventaire();
        }
        // player.setLevel(player.getLevel() + 1);
        System.out.println("[Client "+ player.getId() + "] I AM READY TO ELEVATE! increasing level to " + (player.getLevel() + 1));
        return guardedElevationAttempt();
        

        // return commands();
    }

    public List<Command> decideNextMovesRandom() {
        List<Command> commands = new ArrayList<>();
        Random random = new Random();
    
        CommandType[] possibleCommands = {CommandType.AVANCE, CommandType.GAUCHE, CommandType.DROITE, CommandType.VOIR, CommandType.INVENTAIRE, CommandType.PREND, CommandType.POSE};
        // CommandType[] possibleCommands = CommandType.values();
    
        CommandType randomCommand = possibleCommands[random.nextInt(possibleCommands.length)];
        commands.add(new Command(randomCommand));
    
        return commands;
    }

    /********** ELEVATION **********/

    private boolean readyToElevate() {
        if (targets.isEmpty()) {
            return true;
        }
        return false;
    }

    public List<Command> doElevation() {
        List<Command> commands = new ArrayList<>();
        int level = player.getLevel();
        ElevationRules.Rule rule = ElevationRules.getRule(level);
        Map<Resource, Integer> resourcesNeeded = rule.getResources();

        // do pose of each target
        for (Map.Entry<Resource, Integer> entry : resourcesNeeded.entrySet()) {
            Resource resource = entry.getKey();
            int requiredAmount = entry.getValue();
            for (int i = 0; i < requiredAmount; i++) {
                // add pose command for this resource
                commands.add(new Command(CommandType.POSE, resource.getName()));
                System.out.println("[Client " + player.getId() + "] Posing " + resource.getName());
            }
        }
        // if level > 1 -> broadcast to all players
        // if (level > 1) {
        commands.add(player.broadcastCmd("elevation", "call", level, rule.getPlayers()));
        // }

        // add CommandType.INCANTATION
        commands.add(new Command(CommandType.INCANTATION));
        return commands;
    }

    private List<Command> guardedElevationAttempt() {
        List<Command> commands = new ArrayList<>();
        if (elevationAttemptCooldown > 0) {
            elevationState = ElevationState.COOLDOWN;
            commands.add(new Command(CommandType.VOIR));
            return commands;
        }

        int level = player.getLevel();
        ElevationRules.Rule rule = ElevationRules.getRule(level);
        int playersNeeded = rule.getPlayers();

        if (!hasEnoughPlayersOnCurrentTile(playersNeeded)) {
            elevationState = ElevationState.WAITING_ALLIES;
            if (elevationBroadcastCooldown <= 0) {
                commands.add(player.broadcastCmd("elevation", "call", level, playersNeeded));
                elevationBroadcastCooldown = ELEVATION_BROADCAST_COOLDOWN_TICKS;
            }
            // Keep refreshing local state while waiting for allies.
            commands.add(new Command(CommandType.VOIR));
            return commands;
        }

        elevationState = ElevationState.INCANTING;
        elevationBroadcastCooldown = 0;
        return doElevation();
    }

    private boolean hasEnoughPlayersOnCurrentTile(int playersNeeded) {
        int nearbyPlayers = 0;

        if (!curView.isEmpty()) {
            List<String> tileZero = curView.get(0);
            for (String token : tileZero) {
                if ("player".equals(token) || "joueur".equals(token)) {
                    nearbyPlayers++;
                }
            }
        }

        // Most protocol variants omit self from tile payload; include self explicitly.
        int totalPlayersOnTile = nearbyPlayers + 1;
        return totalPlayersOnTile >= playersNeeded;
    }

    private void tickCooldowns() {
        if (elevationBroadcastCooldown > 0) {
            elevationBroadcastCooldown--;
        }
        if (elevationAttemptCooldown > 0) {
            elevationAttemptCooldown--;
            if (elevationAttemptCooldown == 0 && elevationState == ElevationState.COOLDOWN) {
                elevationState = ElevationState.IDLE;
            }
        }
    }

    private void updateElevationStateForReadiness() {
        if (!readyToElevate() || !inventaireChecked) {
            if (elevationState != ElevationState.GATHERING) {
                elevationState = ElevationState.IDLE;
            }
            return;
        }

        if (elevationState == ElevationState.IDLE || elevationState == ElevationState.GATHERING) {
            elevationState = ElevationState.WAITING_ALLIES;
        }
    }

    private List<Command> searchForTarget() {
        List<Command> commands = new ArrayList<>();

        List<Integer> sortedIndices = getViewIndicesSortedByDistance(player.getLevel());
        for (int tileIdx : sortedIndices) {
            for (Resource target : targets) {
                if (curView.get(tileIdx).contains(target.getName())) {
                    addMovesToTile(tileIdx, target, commands);
                    return commands;
                }
            }
        }

        addRandomMove(commands);
        return commands;
    }

    private List<Command> checkInventaire() {
        List<Command> commands = new ArrayList<>();
        commands.add(new Command(CommandType.INVENTAIRE));
        return commands;
    }

    private void setTargets() {
        int level = player.getLevel();
        ElevationRules.Rule rule = ElevationRules.getRule(level);
        Map<Resource, Integer> resourcesNeeded = rule.getResources();
        // Set<Resource> targets = new EnumSet<>();
        targets.clear();

        for (Map.Entry<Resource, Integer> entry : resourcesNeeded.entrySet()) {
            Resource resource = entry.getKey();
            int requiredAmount = entry.getValue();
            int currentAmount = player.getInventoryCount(resource);
            if (currentAmount < requiredAmount) {
                targets.add(resource); // add to targets
            }
        }
        // return targets;
    }

    /********** FIND **********/

    private List<Command> searchForFood() {
        List<Command> commands = new ArrayList<>();

        int tileIdx = findItemInView(Resource.NOURRITURE);
        if (tileIdx != -1) {
            addMovesToTile(tileIdx, Resource.NOURRITURE, commands);
        } else {
            addRandomMove(commands);
        }
        return commands;
    }

    public int findItemInView(Resource item) {
        List<Integer> sortedIndices = getViewIndicesSortedByDistance(player.getLevel());

        for (int i : sortedIndices) {
            List<String> tileContents = curView.get(i);
            if (tileContents.contains(item.getName())) {
                return i;
            }
        }
        return -1; // item not found
    }

    public int findClosestItem(String item, Position pos) {
        return -1;
    }

    /********** MOVES **********/

    private CommandType getRandomMove() {
        Random random = new Random();
        CommandType[] possibleMoves = {CommandType.AVANCE, CommandType.GAUCHE, CommandType.DROITE};
        return possibleMoves[random.nextInt(possibleMoves.length)];
    }

    // tileIdx in the terms of current view from findItemInView()
    public List<CommandType> getMovesToTile(int tileIdx) {
        List<CommandType> moves = new ArrayList<>();

        if (tileIdx <= 0)
            return moves;
        
        int level = 1;
        int leftIdx = 1;
        while (leftIdx + 2 * level + 1 <= tileIdx) {
            leftIdx += 2 * level + 1;
            level++;
        }

        int center = leftIdx + level;
        int offset = tileIdx - center; // <0 => left, >0 => right
        for (int i = 0; i < level; i++) {
            moves.add(CommandType.AVANCE);
        }
        if (offset < 0) {
            moves.add(CommandType.GAUCHE);
        } else if (offset > 0) {
            moves.add(CommandType.DROITE);
        }
        for (int i = 0; i < Math.abs(offset); i++) {
            moves.add(CommandType.AVANCE);
        }

        return moves;
    }

    private void addMovesToTile(int tileIdx, Resource target, List<Command> commands) {
        List<CommandType> moves = getMovesToTile(tileIdx);
        System.out.println("MOVES to " + target.getName() + "(idx: " + tileIdx + ") : " + moves);
        for (CommandType move : moves) {
            commands.add(new Command(move));
        }
        commands.add(new Command(CommandType.PREND, target.getName()));
    }

    private void addRandomMove(List<Command> commands) {
        commands.add(new Command(getRandomMove()));
        commands.add(new Command(CommandType.VOIR));
    }

    public List<Command> goToElevationCall(int dir, int level, int playersNeeded) {
        List<Command> commands = new ArrayList<>();

        // Direction semantics are server-defined (1..8 around the player, 0 = same tile).
        // This minimal behavior converges toward the caller and refreshes vision.
        if (dir == 0) {
            // If we are already on the caller tile, avoid spamming incantation when group size > 1.
            if (playersNeeded <= 1) {
                elevationState = ElevationState.INCANTING;
                commands.addAll(doElevation());
            } else {
                elevationState = ElevationState.WAITING_ALLIES;
                commands.add(new Command(CommandType.VOIR));
            }
            return commands;
        }

        if (dir == 1 || dir == 2 || dir == 8) {
            commands.add(new Command(CommandType.AVANCE));
        } else if (dir == 3 || dir == 4) {
            commands.add(new Command(CommandType.DROITE));
            commands.add(new Command(CommandType.AVANCE));
        } else if (dir == 5 || dir == 6) {
            commands.add(new Command(CommandType.DROITE));
            commands.add(new Command(CommandType.DROITE));
            commands.add(new Command(CommandType.AVANCE));
        } else if (dir == 7) {
            commands.add(new Command(CommandType.GAUCHE));
            commands.add(new Command(CommandType.AVANCE));
        } else {
            // Unknown direction hint, fall back to a world refresh.
            commands.add(new Command(CommandType.VOIR));
            return commands;
        }

        commands.add(new Command(CommandType.VOIR));
        elevationState = ElevationState.WAITING_ALLIES;

        return commands;
    }

    public void onIncantationResult(boolean success) {
        if (success) {
            elevationState = ElevationState.IDLE;
            elevationAttemptCooldown = 0;
            return;
        }
        elevationState = ElevationState.COOLDOWN;
        elevationAttemptCooldown = ELEVATION_ATTEMPT_COOLDOWN_TICKS;
    }

    /********** UTILS **********/

    // for level=1 this will be:
    // [-1, -1], [0, -1], [1, -1],
    //           [0, 0]
    public List<int[]> generateRelativeOffsets() {
        int level = player.getLevel();
        List<int[]> offsets = new ArrayList<>();
        for (int l = 0; l <= level; l++) {
            for (int i = -l; i <= l; i++) {
                offsets.add(new int[]{i, -l}); // assuming player faces NORTH, adjust later
            }
        }
        return offsets;
    }

    // TODO: implement this directly in prev function
    // for not calling for each tile
    public int[] rotate(int dx, int dy, int direction) {
        switch (direction) {
            case 0: return new int[]{dx, dy};         // ^ NORTH 
            case 1: return new int[]{-dy, dx};        // > EAST
            case 2: return new int[]{-dx, -dy};       // v SOUTH
            case 3: return new int[]{dy, -dx};        // < WEST
            default: throw new IllegalArgumentException("Invalid direction");
        }
    }

    public static List<Integer> getViewIndicesSortedByDistance(int level) {
        List<Integer> result = new ArrayList<>();
        int index = 0;
        result.add(index++); // always start with tile 0 (your current tile)

        for (int l = 1; l <= level; l++) {
            int rowSize = 2 * l + 1;
            int rowStart = index;
            int center = rowStart + rowSize / 2;

            result.add(center);

            for (int offset = 1; offset <= rowSize / 2; offset++) {
                result.add(center - offset); // left
                result.add(center + offset); // right
            }

            index += rowSize; // move to next row for next level
        }

        return result;
    }

    private String createBroadcastMessage(String event, String status, int level, int playersNeeded) {
        JsonObject msg = new JsonObject();
        msg.addProperty("event", event);
        msg.addProperty("status", status);
        msg.addProperty("level", level);
        msg.addProperty("players_needed", playersNeeded);

        return msg.toString();
    }

    /********** SETTERS **********/

    public void setWorld(World world) {
        this.world = world;
    }

    public void setInventaireChecked(boolean inventaireChecked) {
        this.inventaireChecked = inventaireChecked;
        if (!inventaireChecked && elevationState == ElevationState.INCANTING) {
            elevationState = ElevationState.IDLE;
        }
    }

    // public void setDebugLevel(int debugLevel) {
    //     this.debugLevel = debugLevel;
    // }
}