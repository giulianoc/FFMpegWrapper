/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/*
 * File:   FFMPEGEncoder.cpp
 *
 * Created on February 18, 2018, 1:27 AM
 */
#include "FFMpegWrapper.h"
#include "ProcessUtility.h"
#include "StringUtils.h"
#include "spdlog/spdlog.h"
#include <fcntl.h>
#include <fstream>
#include <sys/file.h>
#include <unistd.h>
#include <set>

using namespace std;
using json = nlohmann::json;

namespace
{
// addToIncrontab/removeFromIncrontab possono essere chiamate da thread/processi diversi (un canale live per
// thread). Senza serializzazione, due chiamate concorrenti possono corrompere il file di configurazione
// (read-modify-write senza lock) e, soprattutto, lanciare due 'incrontab <file>' quasi in contemporanea:
// questo fa scattare in incrond due 'table changed, reloading' troppo vicini tra loro, che vanno in race
// nella rimozione dei watch inotify e fanno crashare il servizio (Inotify::Remove -> EINVAL -> unhandled
// exception). Il flock, a differenza di uno std::mutex, serializza sia i thread che eventuali processi diversi.

// Cosa succede se arrivano due addToIncrontab contemporaneamente?
// se due canali (es. canale X e canale Y) chiamano addToIncrontab nello stesso istante, su due thread diversi del processo FFMPEGEncoder:
//  1. Entrambi i thread arrivano a IncrontabFileLock incrontabFileLock(...) e chiamano open() sul file di lock (incrontab.txt.lock) — questo è sicuro farlo in
//  concorrenza, ognuno ottiene il proprio file descriptor sullo stesso file.
//  2. Entrambi chiamano flock(fd, LOCK_EX). Qui avviene la serializzazione vera:
//	- Il thread che arriva "per primo" (diciamo X) ottiene subito il lock e procede.
//	- Il thread Y si blocca dentro flock() — il kernel lo mette in attesa, non prosegue oltre, finché X non rilascia.
//  3. X esegue tutta la sua sezione critica: legge il file, aggiunge la riga per il canale X, scrive il file, esegue /usr/bin/incrontab incrontab.txt e aspetta
//  che finisca (è sincrono).
//  4. Alla fine della funzione (addToIncrontab di X ritorna, con successo o anche con eccezione — non importa), l'oggetto locale incrontabFileLock esce di scope
//  → il suo distruttore fa flock(LOCK_UN) + close(fd) → il lock viene rilasciato.
//  5. Solo ora Y si sveglia, ottiene il lock, e procede: rilegge il file da capo — quindi vede già dentro la riga del canale X che X ha appena scritto — aggiunge
//  la sua riga per Y, scrive il file completo (X + Y), ed esegue a sua volta /usr/bin/incrontab incrontab.txt.
//  6. Anche Y rilascia il lock alla fine.

struct IncrontabFileLock
{
	int fd;

	explicit IncrontabFileLock(const string &incrontabConfigurationPathName) : fd(-1)
	{
		string lockPathName = incrontabConfigurationPathName + ".lock";

		fd = open(lockPathName.c_str(), O_CREAT | O_RDWR, 0666);
		if (fd < 0)
		{
			string errorMessage = std::format("IncrontabFileLock: opening lock file failed, lockPathName: {}", lockPathName);
			LOG_ERROR(errorMessage);
			throw runtime_error(errorMessage);
		}

		const auto start = std::chrono::system_clock::now();
		// Qui avviene la serializzazione
		if (flock(fd, LOCK_EX) != 0)
		{
			string errorMessage = std::format("IncrontabFileLock: flock (LOCK_EX) failed, lockPathName: {}", lockPathName);
			LOG_ERROR(errorMessage);
			close(fd);
			fd = -1;
			throw runtime_error(errorMessage);
		}
		LOG_INFO("IncrontabFileLock: flock (LOCK_EX) acquired"
			", lockPathName: {}"
			", elapsed: {}",
			lockPathName, chrono::duration_cast<chrono::milliseconds>(std::chrono::system_clock::now() - start).count());
	}

	~IncrontabFileLock()
	{
		if (fd >= 0)
		{
			flock(fd, LOCK_UN);
			close(fd);
		}
	}

	IncrontabFileLock(const IncrontabFileLock &) = delete;
	IncrontabFileLock &operator=(const IncrontabFileLock &) = delete;
};
} // namespace

void FFMpegWrapper::addToIncrontab(int64_t ingestionJobKey, int64_t encodingJobKey,
	const string& incrontabScriptPathName, // /opt/mms/MMS/scripts/incrontab.sh
	const string& directoryToBeMonitored, // /var/mms/storage/MMSRepository/MMSLive/6/7228
	const string& cdnDeliveryServersToBeSynched // i.e.: 116.202.53.105 195.160.222.54 68.233.32.34 68.233.45.31 68.233.32.52
)
{
	try
	{
		LOG_INFO(
			"Received addToIncrontab"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", incrontabScriptPathName: {}"
			", directoryToBeMonitored: {}"
			", cdnDeliveryServersToBeSynched: {}",
			ingestionJobKey, encodingJobKey, incrontabScriptPathName, directoryToBeMonitored, cdnDeliveryServersToBeSynched
		);

		// serve il lock lungo tutta la sezione critica (lettura/scrittura del file + esecuzione di incrontab),
		// per questo il path del lock è fisso e non dipende da directoryToBeMonitored
		IncrontabFileLock incrontabFileLock(
			std::format("{}/{}", _incrontabConfigurationDirectory, _incrontabConfigurationFileName));

		if (!fs::exists(_incrontabConfigurationDirectory))
		{
			LOG_INFO(
				"addToIncrontab: create directory"
				", _ingestionJobKey: {}"
				", _encodingJobKey: {}"
				", _incrontabConfigurationDirectory: {}",
				ingestionJobKey, encodingJobKey, _incrontabConfigurationDirectory
			);

			fs::create_directories(_incrontabConfigurationDirectory);
			fs::permissions(
				_incrontabConfigurationDirectory,
				fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec | fs::perms::group_read | fs::perms::group_write |
					fs::perms::group_exec | fs::perms::others_read | fs::perms::others_write | fs::perms::others_exec,
				fs::perm_options::replace
			);
		}

		string incrontabConfigurationPathName = std::format("{}/{}", _incrontabConfigurationDirectory, _incrontabConfigurationFileName);

		bool directoryAlreadyMonitored = false;
		{
			ifstream ifConfigurationFile(incrontabConfigurationPathName);
			if (ifConfigurationFile)
			{
				string configuration;
				while (getline(ifConfigurationFile, configuration))
				{
					string trimmedConfiguration = StringUtils::trim(configuration);

					if (configuration.starts_with(directoryToBeMonitored))
					{
						directoryAlreadyMonitored = true;

						break;
					}
				}

				ifConfigurationFile.close();
			}
		}

		if (directoryAlreadyMonitored)
		{
			string errorMessage = std::format(
				"addToIncrontab: directory is already into the incontab configuration file"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", incrontabConfigurationPathName: {}",
				ingestionJobKey, encodingJobKey, incrontabConfigurationPathName
			);
			LOG_WARN(errorMessage);

			// throw runtime_error(errorMessage);
		}
		else
		{
			ofstream ofConfigurationFile(incrontabConfigurationPathName, ofstream::app);
			if (!ofConfigurationFile)
			{
				string errorMessage = std::format(
					"addToIncrontab: open incontab configuration file failed"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", incrontabConfigurationPathName: {}",
					ingestionJobKey, encodingJobKey, incrontabConfigurationPathName
				);
				LOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
			}

			/*
			 * Lo script incrontabScriptPathName (/opt/mms/MMS/scripts/incrontab.sh) viene chiamato con le seguenti variabili:
				- $@: Path della directory/file monitorato (i.e.: /var/mms/storage/MMSRepository/MMSLive/6/5288)
				- $#: Nome del file che ha generato l'evento (i.e: 5288.m3u8)
				- $%: Eventi/maschera incron che hanno causato l'esecuzione (i.e.: IN_MOVED_TO)
			 */
			string configuration = std::format(
				"{} IN_MODIFY,IN_CLOSE_WRITE,IN_CREATE,IN_DELETE,IN_MOVED_FROM,IN_MOVED_TO,IN_MOVE_SELF {} $% $@ $# \"{}\"",
				directoryToBeMonitored, incrontabScriptPathName, cdnDeliveryServersToBeSynched
			);

			LOG_INFO(
				"addToIncrontab: adding incontab configuration"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", configuration: {}",
				ingestionJobKey, encodingJobKey, configuration
			);

			ofConfigurationFile << configuration << endl;
			ofConfigurationFile.close();
		}

		{
			string incrontabExecuteCommand = std::format("{} {}", _incrontabBinary, incrontabConfigurationPathName);

			LOG_INFO(
				"addToIncrontab: Executing incontab command"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", incrontabExecuteCommand: {}",
				ingestionJobKey, encodingJobKey, incrontabExecuteCommand
			);

			int executeCommandStatus = ProcessUtility::execute(incrontabExecuteCommand);
			if (executeCommandStatus != 0)
			{
				string errorMessage = std::format(
					"addToIncrontab: incrontab command failed"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", executeCommandStatus: {}"
					", incrontabExecuteCommand: {}",
					ingestionJobKey, encodingJobKey, executeCommandStatus, incrontabExecuteCommand
				);
				LOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
			}
		}
	}
	catch (...)
	{
		string errorMessage = std::format(
			"addToIncrontab failed"
			", ingestionJobKey: {}"
			", encodingJobKey: {}",
			ingestionJobKey, encodingJobKey
		);
		LOG_ERROR(errorMessage);
	}
}

void FFMpegWrapper::removeFromIncrontabAndSanityCheck(int64_t ingestionJobKey, int64_t encodingJobKey, const string& directoryToBeMonitored)
{
	try
	{
		LOG_INFO(
			"Received removeFromIncrontabAndSanityCheck"
			", ingestionJobKey: {}"
			", encodingJobKey: {}"
			", directoryToBeMonitored: {}",
			ingestionJobKey, encodingJobKey, directoryToBeMonitored
		);

		string incrontabConfigurationPathName = std::format("{}/{}",
			_incrontabConfigurationDirectory, _incrontabConfigurationFileName);

		IncrontabFileLock incrontabFileLock(incrontabConfigurationPathName);

		bool foundMonitoryDirectory = false;
		vector<string> vConfiguration;
		{
			ifstream ifConfigurationFile(incrontabConfigurationPathName);
			if (!ifConfigurationFile)
			{
				string errorMessage = std::format(
					"removeFromIncrontabAndSanityCheck: open incontab configuration file failed"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", incrontabConfigurationPathName: {}",
					ingestionJobKey, encodingJobKey, incrontabConfigurationPathName
				);
				LOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
			}

			set<string> directories;	// per sanare le righe duplicate
			string configuration;
			while (getline(ifConfigurationFile, configuration))
			{
				string trimmedConfiguration = StringUtils::trim(configuration);

				auto pos = trimmedConfiguration.find(' ');
				if (pos == string::npos)
				{
					LOG_ERROR(
						"removeFromIncrontabAndSanityCheck (clean up): wrong incrontab configuration"
						", ingestionJobKey: {}"
						", encodingJobKey: {}"
						", configuration: {}",
						ingestionJobKey, encodingJobKey, configuration
					);

					continue;
				}

				std::string configurationDirectory = trimmedConfiguration.substr(0, pos);

				// restituisce false sia se la directory non esiste sia se il path esiste ma non è una directory
				if (!filesystem::is_directory(configurationDirectory))
				{
					LOG_ERROR(
						"removeFromIncrontabAndSanityCheck (clean up): wrong incrontab configuration, directory does not exist"
						", ingestionJobKey: {}"
						", encodingJobKey: {}"
						", configurationDirectory: {}",
						ingestionJobKey, encodingJobKey, configurationDirectory
					);

					continue;
				}

				// insert() restituisce una pair, e .second indica se l'inserimento è avvenuto
				if (!directories.insert(configurationDirectory).second)
				{
					// directory già presente
					LOG_INFO(
						"removeFromIncrontabAndSanityCheck (clean up): removing incontab configuration, directory already monitored"
						", ingestionJobKey: {}"
						", encodingJobKey: {}"
						", configuration: {}",
						ingestionJobKey, encodingJobKey, configuration
					);

					continue;
				}
				// directory inserita per la prima volta

				if (configurationDirectory == directoryToBeMonitored)
				{
					LOG_INFO(
						"removeFromIncrontabAndSanityCheck: removing incontab configuration"
						", ingestionJobKey: {}"
						", encodingJobKey: {}"
						", configuration: {}",
						ingestionJobKey, encodingJobKey, configuration
					);

					foundMonitoryDirectory = true;

					continue;
				}

				vConfiguration.push_back(trimmedConfiguration);
			}
		}

		if (!foundMonitoryDirectory)
		{
			string errorMessage = std::format(
				"removeFromIncrontabAndSanityCheck: monitoring directory is not found into the incrontab configuration file"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", incrontabConfigurationPathName: {}",
				ingestionJobKey, encodingJobKey, incrontabConfigurationPathName
			);
			LOG_WARN(errorMessage);
		}

		// in ogni caso riscrivo la configurazione
		{
			ofstream ofConfigurationFile(incrontabConfigurationPathName, ofstream::trunc);
			if (!ofConfigurationFile)
			{
				string errorMessage = std::format(
					"removeFromIncrontabAndSanityCheck: open incontab configuration file failed"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", incrontabConfigurationPathName: {}",
					ingestionJobKey, encodingJobKey, incrontabConfigurationPathName
				);
				LOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
			}

			for (const string& configuration : vConfiguration)
				ofConfigurationFile << configuration << endl;
			ofConfigurationFile.close();
		}

		{
			string incrontabExecuteCommand = std::format("{} {}", _incrontabBinary, incrontabConfigurationPathName);

			LOG_INFO(
				"removeFromIncrontabAndSanityCheck: Executing incontab command"
				", ingestionJobKey: {}"
				", encodingJobKey: {}"
				", incrontabExecuteCommand: {}",
				ingestionJobKey, encodingJobKey, incrontabExecuteCommand
			);

			int executeCommandStatus = ProcessUtility::execute(incrontabExecuteCommand);
			if (executeCommandStatus != 0)
			{
				string errorMessage = std::format(
					"removeFromIncrontabAndSanityCheck: incrontab command failed"
					", ingestionJobKey: {}"
					", encodingJobKey: {}"
					", executeCommandStatus: {}"
					", incrontabExecuteCommand: {}",
					ingestionJobKey, encodingJobKey, executeCommandStatus, incrontabExecuteCommand
				);
				LOG_ERROR(errorMessage);

				throw runtime_error(errorMessage);
			}
		}
	}
	catch (...)
	{
		string errorMessage = std::format(
			"removeFromIncrontabAndSanityCheck failed"
			", ingestionJobKey: {}"
			", encodingJobKey: {}",
			ingestionJobKey, encodingJobKey
		);
		LOG_ERROR(errorMessage);
	}
}
