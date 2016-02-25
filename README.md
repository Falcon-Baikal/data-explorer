Data Explorer
====================

<img src="https://cloud.githubusercontent.com/assets/6119070/11098722/66e4ad1c-886c-11e5-9bd2-097b15457102.png">



Data Explorer is a multi-platfrom graphical browser for data files.
Currently, the 
[netCDF](http://www.unidata.ucar.edu/software/netcdf) file format is supported.

netCDF support includes
[DAP](http://opendap.org).

Road map plans include supporting the HDF file format and the SQLite database format. 

Dependencies
------------

<img src="https://cloud.githubusercontent.com/assets/6119070/13334137/231ea0f8-dbd0-11e5-8546-8a409d80aa6d.png">

[Qt](http://www.qt.io/)
Qt is a cross-platform application framework for creating graphical user interfaces.
<br /> 

[netCDF](http://www.unidata.ucar.edu/software/netcdf)
netCDF is a set of software libraries and self-describing, 
machine-independent data formats that support the creation, 
access, and sharing of array-oriented scientific data.
<br /> 

Building from source
------------


Install dependency packages (Ubuntu):
<pre>
sudo apt-get install build-essential
sudo apt-get install libgl1-mesa-dev
sudo apt-get install libnetcdf-dev netcdf-bin netcdf-doc
</pre>

Get source:
<pre>
git clone https://github.com/pedro-vicente/data-explorer.git
</pre>

Build with:
<pre>
qmake
make
</pre>


To generate the included netCDF sample data in /data/netcdf:

<pre>
ncgen -k netCDF-4 -b -o data/netcdf/test_01.nc data/netcdf/test_01.cdl
ncgen -k netCDF-4 -b -o data/netcdf/test_02.nc data/netcdf/test_02.cdl
ncgen -k netCDF-4 -b -o data/netcdf/test_03.nc data/netcdf/test_03.cdl
</pre>

test_01.cdl includes one, two and three dimensional variables with coordinate variables 
<br /> 
test_02.cdl includes a four dimensional variable 
<br /> 
test_03.cdl includes a five dimensional variable
<br /> 

To run and open a sample file from the command line:

<pre>
./explorer data/netcdf/test_03.nc
</pre>

<a target="_blank" href="http://www.space-research.org/">
<img src="https://cloud.githubusercontent.com/assets/6119070/11140582/b01b6454-89a1-11e5-8848-3ddbecf37bf5.png"></a>


