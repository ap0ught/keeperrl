keeperrl
========

Source code of KeeperRL.

http://keeperrl.com,

http://store.steampowered.com/app/329970,

https://miki151.itch.io/keeperrl


Compiling
=========

On Windows: go to here -> http://keeperrl.com/compiling-keeperrl-on-windows/

On Linux and Mac:

*Prerequisites*

  * make essentials
  * gcc-4.8.2 OR clang-3.5
  * git
  * For compiling on OSX you'll also need libboost-system, libboost-thread and libboost-chrono
  * libsdl2-dev, libsdl2-image-dev
  * libopenal-dev
  * libvorbis-dev
  * libtheora-dev

Under Ubuntu 14.4 , you could use these to create development enviroment 
```
sudo apt-get install libsdl2-dev libsdl2-image-dev libopenal-dev libvorbis-dev libtheora-dev llvm-3.4 build-essential

```


In terminal:  
  ```
  git clone https://github.com/miki151/keeperrl.git
  cd keeperrl
  make -j 8 OPT=true RELEASE=true # Add DEBUG=true to have debug symbols
  # add OSX=true to compile on OSX
  ./keeper
  ```


Using Paid Steam Assets
=======================

If you own the paid version of KeeperRL on Steam, you can reuse its assets when
building from source. On Windows installations where Steam is configured to use
`D:\SteamLibrary\steamapps\common\KeeperRL`, copy the entire `data` directory
from that location into the root of this repository so that it sits alongside
`data_free/`. Your tree should look like:

```
keeperrl/
  data/
  data_free/
  ...
```

When the game launches it will detect the presence of `data/` and load the paid
art, audio, and video resources automatically.
