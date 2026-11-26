# Stash

[Stash](https://github.com/stashapp/stash) is a self-hosted organizer for adult scenes. This module browses a Stash server read-only over its GraphQL API and hands playback to mpv, the same way the Plex, Jellyfin and Emby modules do.

## To enable

1. In Stash open **Settings → Security → API Key** and copy the key.
2. Create `stash.json` in the 240-MP data directory:

   | Platform | Path |
   |---|---|
   | macOS | `~/Library/Application Support/240-MP/stash.json` |
   | Raspberry Pi OS / Linux / SteamOS | `~/.local/share/240-MP/stash.json` |

   ```json
   {
     "serverUrl": "http://192.168.1.50:9999",
     "apiKey": "YOUR_STASH_API_KEY"
   }
   ```

   - `serverUrl` must include `http://` or `https://` and must not end in a path (a pasted `/graphql` is stripped automatically).
   - `apiKey` may be omitted only when the Stash server allows unauthenticated GraphQL access; most setups need it.
   - The file is re-read every time the module is entered, so edits apply on the next visit — no restart or rebuild needed.

3. Restrict the file, since it holds a secret:

   ```bash
   chmod 600 <data dir>/stash.json
   ```

4. In 240-MP open **Settings → Modules → Stash** and set **Enabled** to ON.

Stash **v0.23 or newer** is required: the module replays saved filters through `find_filter` / `object_filter`, which were added in that release.

## What you get

- **Scenes** — `ALL SCENES` plus every saved scene filter on the server, sorted by name. Selecting one lists that filter's scenes.
- **Tags** — every tag, sorted by name. Selecting one lists the scenes that carry that tag.
- Scene rows lead with the title (falling back to the video filename, then `SCENE <id>`.
- Selecting a scene plays it fullscreen through mpv; exiting returns to the same position in the list.
- Subtitles start off (`--sid=no`), matching the other modules' default. There is no subtitle picker in this module.

## What this module does not do

- It never writes to Stash — no resume positions, play counts, ratings or tag changes. Playback always starts at the beginning.
- There is no settings UI for the server or key (the config file is the interface, by design).
- No artwork/thumbnails, no pagination, no NFC hand-off. The whole list is requested in one query, matching the other modules' browse lists.

## Troubleshooting

| Screen says | Fix |
|---|---|
| `STASH.JSON NOT FOUND` | Create the file at the path shown on screen. |
| `INVALID JSON IN STASH.JSON` | Fix the JSON — a trailing comma is the usual cause. |
| `SERVERURL MISSING IN STASH.JSON` | Add `"serverUrl"`. |
| `SERVERURL MUST START WITH HTTP:// OR HTTPS://` | Include the scheme, e.g. `http://stash.local:9999`. |
| `AUTH FAILED - CHECK APIKEY IN STASH.JSON` | Re-copy the key from Stash → Settings → Security. |
| `INVALID RESPONSE FROM SERVER - CHECK SERVERURL` | The URL does not point at a Stash server, or a reverse proxy is answering with HTML. |
| `LOAD SCENES FAILED: …` | The GraphQL message from Stash follows the colon — usually a permissions or schema-version problem. |
| `NO SCENES FOUND` | The filter or tag has no scenes, or none of its scenes have a playable video file (scenes without a stream are hidden). |

Self-signed certificates are accepted only for the host in `serverUrl`, and only for the certificate errors a local install typically produces. That relaxation covers browsing (the GraphQL calls); playback is handed to mpv, which does its own TLS verification. If Stash is served over HTTPS with a self-signed certificate, either install that certificate's CA in the OS trust store or use plain HTTP on a trusted LAN.

If Stash runs behind a reverse proxy, set Stash's `external_host` (and have the proxy send the usual `X-Forwarded-*` headers) so the stream URLs Stash returns point at the proxy. This module uses those URLs as given.
