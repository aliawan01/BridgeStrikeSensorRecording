# Bridge Strike Sensor Detection

This project was made for the purpose of recording the data being collected by the [HBK G-Link](https://www.hbkworld.com/en/products/instruments/wireless-daq-systems/wireless-nodes/g-link-200)
and storing that data in a parquet file which is then sent to an Azure Blob Container. The project also contains a settings UI which will allow you to configure which wireless nodes
are in your network as well as the channels, triggers, pre/post recording duration and other settings which are available on each node.

## Building 
Building this project is going to be a painful experience, and you have been warned, so please proceed at your own risk ;). I haven't included all of the dependencies necessary to build the project since:  
a) This repo will be massive  
b) Most of these dependencies need to be built manually since they're outdated and aren't readily available on most package repos

The dependencies which are still available and modern C++ package repos are included in the `vcpkg.json` file and if you have vcpkg [installed](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-powershell)
then all you need to do to install these dependencies is run `vcpkg install`.

The dependencies which need to be installed manually are the following
- [Boost](https://www.boost.org/releases/1.68.0/) == 1.68
- [MSCL](https://github.com/HBK-MicroStrain/MSCL)


Once you've built these libraries then please put all of the libraries and header files produced
from MSCL into the `MSCL` folder and the same for boost under the `boost` folder.

You can then run `./build.sh` and hopefully it will output some binaries!

## Running 
After building the project:

- To run the main sensor collection program run `./run_server.sh`
- To run the settings menu run `./run_client.sh` (the settings menu will not show until the main program has connnected to all of the sensors)
