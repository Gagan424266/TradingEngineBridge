#pragma once
#include <memory>
#include <string>

#include "SQLAPI.h"

struct ConnectionDetails {
	ConnectionDetails(const std::string &ip, long port, const std::string &databaseName, const std::string &username, const std::string &password);

	std::string m_Ip;
	long m_Port;
	std::string m_DatabaseName;
	std::string m_UserName;
	std::string m_Password;
};

class PostgresDBManager {
private:
	SAConnection m_Conn;
	ConnectionDetails m_ConnectionDetails;
public:
	PostgresDBManager(const std::string &ip, long port, const std::string &databaseName, const std::string &username, const std::string &password);
	~PostgresDBManager();
	void connect();
	std::unique_ptr<SACommand> executeQuery(const std::string &str);
};
