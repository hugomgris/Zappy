# zappy


# install mkcert windows
    - Download https://github.com/FiloSottile/mkcert/releases for your arch (windows-amd64)
    - Move it to wherever you want
    - Open powershell as admin and navigate to it's folder.
    - run 'mkcert -install'
    - run 'mkcert localhost 127.0.0.1 ::1'
    -change server/config key and cert to the ones provided my mkcert (them are on it's folder)

Para la GUI:
- abrir godot
- importar el archivo zappy_godot/project.godot
- para lanzar el juego:
	-(fn)F5 o play arriba a la derecha



cambios cliente -> mvn compile.

cliente -> './client -n team2 -p 8674'
opcion '-c' lanzas n clientes

## Full Session Launcher

For a no-GUI match run from the repo root:

```bash
./run_session.sh
```

The launcher starts the server first, then the client processes, and finally calls `server/run.sh` to resume the time API. It watches client logs for the `player level advanced to 8` marker and stops the session when a winner is detected.

Useful environment variables:

- `ZAPPY_TEAM_ONE_CLIENTS` and `ZAPPY_TEAM_TWO_CLIENTS` (defaults to the server's full per-team capacity)
- `ZAPPY_TEAM_ONE_NAME` and `ZAPPY_TEAM_TWO_NAME`
- `ZAPPY_MAX_SECONDS`
- `ZAPPY_LOG_DIR`