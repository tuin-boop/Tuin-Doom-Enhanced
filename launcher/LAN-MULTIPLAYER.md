# LAN multiplayer

Open **F4 > Multiplayer > Host / join LAN game**. Multiplayer uses the same small font, colours, sidebar and selection style as F4. It has its own F4 section; the extra classic main-menu entry is removed. The launcher's Multiplayer button opens the same lobby directly.

1. Both computers need this same build, IWAD and add-ons in the same order.
2. Host: choose map, difficulty and mode before selecting **Host new game**, then **Start match**. You can start alone.
3. Other players select **Browse LAN games**, choose a host, and press Enter. The list refreshes automatically and shows player counts, lobby/playing/full status and content mismatches. Press R to rescan. You can join running matches. **Join by IP** remains available if broadcasts are blocked.
4. Late arrivals reconstruct the running match, verify its state and spawn at a clear player-start location. Longer matches take longer to catch up. The host keeps playing.

Up to four players, IPv4 LAN, TCP port 5031; automatic discovery uses UDP port 5032. Allow the game through Windows Firewall on your private network. Guest Wi-Fi isolation can block connections. Opening F4 does not pause a network match. Closing the multiplayer menu does not disconnect a running session.

Fluid Weapons and fast-pistol timing follow the host's settings, fixed for the match. Fluid monster behaviour, destruction/door blowouts, custom AI and lockdowns remain disabled in network matches.

- F8 opens live photo mode: orbit, zoom, subject selection, depth of field, colour filters and F7 screenshots remain available. The match keeps running and you can still take damage. Frame stepping and automatic freeze triggers are unavailable in multiplayer.
- Hold Q to select a weapon. Multiplayer runs at normal speed while the wheel is open; selection is synchronized with the other players.
- Enable mouse look in the usual settings to look and aim up/down. Each player's aim is synchronized independently; players without mouse look retain autoaim.
- The host selects **Co-op friendly fire** before starting the match. Off blocks damage and knockback from teammates, including splash damage; self-damage remains. An occupied teammate teleport destination is blocked rather than overlapping players. Deathmatch is unaffected.
- **Character / suit colour** selects Green, Gray, Brown or Red. It can be changed during play and is synchronized for other players. The accelerated sprite and voxel renderers use Doom's suit palette translation. Local single-player settings are preserved. Custom maps need suitable player starts and clear spawning space. This is not support for arbitrary ZScript gameplay mods.

The lobby verifies hashes of the engine, IWAD, bundled resource packs and selected add-ons. The host sequences player commands over TCP; clients replay identical simulation frames and compare state hashes. A joining player is activated at an explicit frame boundary. No native memory/savegame blobs are transferred. Command history is bounded to 500,000 frames (about four hours, under 48 MB); after that, established players continue but new late joins require a new match. TCP buffers and catch-up work per frame are bounded. The old command-line UDP transport remains available separately.

Validated with memory-monitored headless processes: host starts alone, three staggered late joins with Fluid Weapons, all four players active, 537 shared sampled states with zero differences; every received frame also verified by the transport. Live photo/wheel ticking, per-player aim and colours, synchronized wheel weapon changes, and friendly-fire off/on/self-damage checks passed. All 29 regression tests passed. Physical LAN/Wi-Fi behaviour and accelerated rendering still need testing on two PCs. GPU stress runs remain disabled after the earlier memory-pressure incident.
