#include "PostgresDBManager.hpp"
#include "Logger.h"


ConnectionDetails::ConnectionDetails(const std::string &ip, long port, const std::string &databaseName, const std::string &username, const std::string &password) {
	m_Ip = ip;
	m_Port = port;
	m_DatabaseName = databaseName;
	m_Password = password;
	m_UserName = username;
}

PostgresDBManager::PostgresDBManager(const std::string &ip, long port, const std::string &databaseName, const std::string &username, const std::string &password): m_ConnectionDetails(ip, port, databaseName, username, password)  {
	try {
		connect();
	} catch (const SAException &e) {
		const std::string err = (const char *)e.ErrText();
		throw std::runtime_error(err);
	}	
}

void PostgresDBManager::connect() {
	const std::string dbString = m_ConnectionDetails.m_Ip + "," + std::to_string(m_ConnectionDetails.m_Port) + "@" + m_ConnectionDetails.m_DatabaseName;
	m_Conn.Connect(dbString.c_str(), m_ConnectionDetails.m_UserName.c_str(), m_ConnectionDetails.m_Password.c_str(), SA_PostgreSQL_Client);
	
	m_Conn.setIsolationLevel(SAIsolationLevel_t::SA_Serializable); // decide if we want this https://www.geeksforgeeks.org/transaction-isolation-levels-dbms/
	m_Conn.setAutoCommit(SAAutoCommit_t::SA_AutoCommitOn);
	
	LOG_INFO("Successfully connected with database: " + m_ConnectionDetails.m_Ip + ":" + std::to_string(m_ConnectionDetails.m_Port) + "/" + m_ConnectionDetails.m_DatabaseName);
}

PostgresDBManager::~PostgresDBManager() {
	try {
		m_Conn.Disconnect();
		LOG_INFO("Database disconnected");
	} catch (const SAException &e) {
		LOG_ERROR("Could not disconnect: " + std::string((const char *)e.ErrText()));
	}
}

std::unique_ptr<SACommand> PostgresDBManager::executeQuery(const std::string &str) {
	try {	
		std::unique_ptr<SACommand> cmd = std::make_unique<SACommand>();
		if (not m_Conn.isConnected() or not m_Conn.isAlive()) {
			LOG_WARN("Server disconnected. Reconnecting...");
			connect();
		}
		cmd->setConnection(&m_Conn);
		cmd->setCommandText(str.c_str());
		cmd->Execute();
		return std::move(cmd);
	} catch (const SAException &e) {
		LOG_ERROR("PostgresDBManager::executeQuery - SAException: " + std::string((const char *)e.ErrText()));
		return std::unique_ptr<SACommand>(nullptr);
	}
}

