# All in one server

An all in one C++ server to serve my personal webpage, monitor bridge state, participate in home automation. Very specific to my home automation setup. Also includes scripts and systemd service configurations to enable the 802.11<->802.3 bridge.

Contains a authenticated reverse proxy implementation for OpenClaw access (instead of a tailscale configuration as suggested).

Initial implementation was completed by Gemini 3 and I. It faithfully implemented my (bad) architecture. I subsequently made most changes by hand, not because Gemini was bad at software engineering, but because I like to program, architect and solve problems. I am now completing all changes on my own, unassisted by any AI coding tool.

## Compilation
Compilation and transfer commands are in redeploy.sh.

Requires Boost, C++26 and liburing, and {npm, mui, react} for the frontend.

PDF copies of my blog posts are in the frontend/public directory.
