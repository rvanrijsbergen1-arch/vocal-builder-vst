# Issuing and managing customer licences

Each customer gets their own **signed licence key**. Each computer they use gets a **signed
activation code** bound to that machine. The plugin and the processing engine both check the two
signatures with the public key built into the product. A shared serial or an editable
`activated=true` flag would not stop anyone, so neither is used.

## One-time setup (on your own computer, never on a customer's)
```
pip install cryptography
python tools/licence_admin.py init
```
This creates `~/VocalBuilderLicensing/private_key.pem` and `licences.db`, and prints one C++ line.
Paste that line into `common/PublicKey.h`, set `kIsDevelopmentKey = false`, and rebuild.
**Back up the private key folder.** If you lose it you cannot issue licences for this build. If it
leaks, anyone can make licences, and you have to ship an update with a new key.

## Day to day
| Task | Command |
|---|---|
| New sale | `python tools/licence_admin.py issue --customer "Jan Jansen" --email jan@example.com --devices 2`, then e-mail the printed `VB1.…` key |
| Customer activates a computer | They paste the key in the plugin (Licence button), copy the `VBR1.…` *activation request* and send it to you. Run `python tools/licence_admin.py activate "VBR1.…"` and send back the `VBA1.…` code. |
| See all licences | `python tools/licence_admin.py list` |
| One customer's history and devices | `python tools/licence_admin.py show VB-0001` |
| Move to a new PC (transfer) | `python tools/licence_admin.py deactivate VB-0001 <machine-id>`, then activate the new request |
| Lost or leaked key | `python tools/licence_admin.py replace VB-0001` (new key, old one marked replaced) |
| Refund or chargeback | `python tools/licence_admin.py revoke VB-0001 --reason refund` |
| Spreadsheet export | `python tools/licence_admin.py export licences.csv` |

## Option A (built now): offline signed licences
- No server and no running costs. The plugin never needs the internet.
- You do one manual step per computer (the activation code). This can be automated by e-mail or a web form later.
- **Limits:** you cannot see whether a licence is in use right now, and you cannot switch off a
  computer that is already activated. `revoke` only stops *new* activations.

## Option B (planned, optional): online activation once, then offline
A small activation service (for example a serverless function plus a database) would do the
`activate` step automatically when the customer clicks Activate. After that the plugin works
offline exactly as now.
- Gains: instant self-service activation, live device counts, transfers without e-mail.
- Costs: you host and maintain a small service. Hosting may be free at low volume but is a recurring responsibility.
- Even with a server, an activated offline computer cannot be revoked until it checks in again.
  The plugin will never stop working mid-session because the service is unreachable.

The licence format already supports Option B: the server would sign the same `VBA1` codes with
the same private key.
