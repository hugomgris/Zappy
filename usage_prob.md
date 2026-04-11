chmod +x probe.sh

./probe.sh                                      # standard 12v12, 900s
./probe.sh --easy-ascension --max-seconds 120   # sanity check (fast win)
./probe.sh --team1 6 --team2 6 --no-fork        # small session, no forking
./probe.sh --time-unit 200 --max-seconds 1800   # slower server, longer session
