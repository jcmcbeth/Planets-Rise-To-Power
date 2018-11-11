# Planets: Rise To Power
SWR 2.0 MUD

Not to be confused with the SWR 1.0 mud called Rise IN Power.

This is an SWR 2.0 MUD that I worked on for a short period. It is based on a SWR 2.0 refactor that I can't find the origin of. If you do know please sent me the details.

## How to build
Using Ubuntu Server 18.10 use the following commands:
```bash
sudo apt-get install csh make gcc libz-dev

git clone https://github.com/jcmcbeth/Planets-Rise-To-Power.git
cd swr2/src

make all

cd ..
chmod +x run-swr

./run-swr &
```
