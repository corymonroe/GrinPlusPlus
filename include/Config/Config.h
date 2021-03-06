#pragma once

#include <Config/ConfigProps.h>
#include <Config/NodeConfig.h>
#include <Config/WalletConfig.h>
#include <Config/ServerConfig.h>

#include <Config/Environment.h>
#include <Config/Genesis.h>
#include <Config/TorConfig.h>
#include <string>
#include <filesystem.h>
#include <Common/Util/FileUtil.h>

class Config
{
public:
	static std::shared_ptr<Config> Load(const Json::Value& json, const EEnvironmentType environment)
	{
		std::string dataDir = StringUtil::Format("{}/.GrinPP/{}/", FileUtil::GetHomeDirectory(), Env::ToString(environment));

		if (json.isMember(ConfigProps::DATA_PATH))
		{
			dataDir = json.get(ConfigProps::DATA_PATH, dataDir).asString();
		}

		FileUtil::CreateDirectories(dataDir);
		
		return std::make_shared<Config>(Config(json, environment, FileUtil::ToPath(dataDir)));
	}

	static std::shared_ptr<Config> Default(const EEnvironmentType environment)
	{
		return Load(Json::Value(), environment);
	}

	Json::Value& GetJSON() { return m_json; }

	const std::string& GetLogLevel() const { return m_logLevel; }
	const Environment& GetEnvironment() const { return m_environment; }
	const fs::path& GetDataDirectory() const { return m_dataPath; }
	const fs::path& GetLogDirectory() const { return m_logPath; }
	const NodeConfig& GetNodeConfig() const { return m_nodeConfig; }

	const WalletConfig& GetWalletConfig() const { return m_walletConfig; }
	const ServerConfig& GetServerConfig() const { return m_serverConfig; }
	const TorConfig& GetTorConfig() const { return m_torConfig; }

private:
	Config(const Json::Value& json, const EEnvironmentType environment, const fs::path& dataPath)
		: m_json(json),
		m_environment(environment),
		m_dataPath(dataPath),
		m_nodeConfig(m_json, dataPath),
		m_walletConfig(m_json, environment, m_dataPath),
		m_serverConfig(m_json, environment),
		m_torConfig(json)
	{
		fs::create_directories(m_dataPath);

		m_logPath = FileUtil::ToPath(m_dataPath.u8string() + "LOGS/");
		fs::create_directories(m_logPath);

		m_logLevel = "DEBUG";
		if (json.isMember(ConfigProps::Logger::LOGGER))
		{
			const Json::Value& loggerJSON = json[ConfigProps::Logger::LOGGER];
			m_logLevel = loggerJSON.get(ConfigProps::Logger::LOG_LEVEL, "DEBUG").asString();
		}
	}

	Json::Value m_json;

	fs::path m_dataPath;
	fs::path m_logPath;

	std::string m_logLevel;
	Environment m_environment;
	NodeConfig m_nodeConfig;
	WalletConfig m_walletConfig;
	ServerConfig m_serverConfig;
	TorConfig m_torConfig;
};

typedef std::shared_ptr<const Config> ConfigPtr;