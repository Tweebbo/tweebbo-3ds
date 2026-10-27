# Nintendo 3DS player guide

[Download and install](../README.md#install) · [Release notes](RELEASE.md)

## Rooms and furniture

The Navigator lists public rooms, other players' rooms and your own. Use My Rooms to create a room or manage its access. Password-protected rooms ask for the password when you enter.

Browse the Catalogue to preview furniture and prices on the top screen. Purchases go to the Hand, where you can group items and choose Place in room. Drag long lists with the stylus to scroll.

In build mode:

| Control | Action |
| --- | --- |
| Circle Pad / D-Pad | Move the furniture preview |
| L / R | Rotate; lower or raise wall furniture |
| A | Place |
| B | Cancel |
| Minimap | Jump to a tile |

A green preview fits; red means the hotel will reject the placement. Select placed furniture to move, rotate, pick up or use it. Room owners can switch on dance floors and other walk-on furniture with lights from the furniture window or nearby A prompt.

When you stand near a seat, bed, dispenser, dice, gate or teleporter, the top screen shows the available A action. Away from furniture, A opens chat.

## Friends, looks and pets

Me contains gestures, drinks, the wardrobe and your character code. Friends lets you visit contacts, manage requests, ignore players and exchange private messages. Select another player to open their card or start a furniture trade.

Trades support up to twelve furniture slots. Both players accept and confirm again; changing either offer resets both confirmations. Credits cannot be traded.

Adopt pets with a species, coat, colour and name, then place them in a room to use their commands and view their status. Club membership and profile badges follow your character across devices. Support in Me opens the donation information and a QR link to the website.

## Games and offline practice

The Games and Casino buttons sit in the top corners of the touch-screen map. Game Center opens Fast Food, Battle Ball and Snow Storm. You can also challenge someone in the room to Tic-tac-toe, Connect Four or Chess.

Snow Storm targets the nearest rival automatically. Tap a rival to choose a target, or tap again to return to automatic targeting. Casino games on the console use credits; real-money chips are available only in the PC edition.

Help → Offline opens the local public-room simulation with bots. A connection failure shows an error rather than silently moving you offline.

## Saved progress

The hotel saves your credits, furniture, rooms, look, friends and pets on the server. The console keeps your character key and last room on the SD card, so updates and reinstalls keep the same character when that data remains available.

Save your private code from Me → Character code. Use a code restores that character on another installation or the web. Keep the code and `sdmc:/3ds/Tweebbo/token-<server>.txt` private: they give access to your character.

## Updates and troubleshooting

On entering the hotel, Tweebbo checks the latest GitHub release and offers Update now when a newer version is available. Help → Check for updates checks again.

The updater verifies the download's SHA-256. CIA installations use the console's AM service and then ask you to restart. If installation fails, the CIA is saved to `SD:/cias/` for installation with FBI. Homebrew Launcher installations download the `.3dsx` instead.

Help → Check tweebbo.lol tests the secure connection without creating a character. Connection errors are saved in `sdmc:/3ds/Tweebbo/last-error.txt`; `log.txt` records the model, memory, texture failures and update activity. Remove private codes and tokens before sharing diagnostics.

For isolated LAN testing, use the separate [debug build](DEVELOPING.md#local-testing).

![The web client sees a Nintendo 3DS player](visual/crossplay-web.png)
