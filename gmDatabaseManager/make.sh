export LD_LIBRARY_PATH="/home/kenil/gmDBManager/SQLAPI_new/lib/"
echo "Bhavadip hi"
export LD_LIBRARY_PATH="/home/bhavadip/code/Database_Common/Database_Common/gmDBManagerNew/SQLAPI_new/lib"
g++ ./source/gmConfigurationMaster.cpp ./source/test.cpp ./source/PostgresDBManager.cpp -o connect -L ./SQLAPI_new/lib -lsqlapi -ldl
