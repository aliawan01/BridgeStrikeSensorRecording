#Bridge Strike Sensor Detection

This project was made for the purpose of recording the data being collected by the [HBK G-Link](https://www.hbkworld.com/en/products/instruments/wireless-daq-systems/wireless-nodes/g-link-200)
and storing that data in a parquet file which is then sent to an Azure Blob Container. The project also contains a settings UI which will allow you to configure which wireless nodes
are in your network as well as the channels and triggers which are enabled on each node.

## Building 
Building this project is a arduous endeavor, so please proceed at your own risk. I haven't included all of the dependencies necessary to build the project since:
a) This repo will be massive
b) Most of these dependencies need to be built manually since they're outdated and aren't readily available on most package repos

The dependencies which are still available and modern C++ package repos are included in the `vcpkg.json` file and if you have vcpkg [installed](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-powershell)
then all you will need to do to install these dependencies is run `vcpkg install`.

The dependencies which need to be installed manually are the following
- [Boost](https://www.boost.org/releases/1.68.0/) == 1.68
- [MSCL](https://github.com/HBK-MicroStrain/MSCL)
