/*
 *      Copyright (C) 2012-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "Application.h"
#include "ApplicationMessenger.h"
#include "GUIInfoManager.h"
#include "dialogs/GUIDialogOK.h"
#include "dialogs/GUIDialogNumeric.h"
#include "dialogs/GUIDialogProgress.h"
#include "dialogs/GUIDialogExtendedProgressBar.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogYesNo.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "music/tags/MusicInfoTag.h"
#include "settings/AdvancedSettings.h"
#include "settings/MediaSettings.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "windows/GUIWindowPVR.h"
#include "utils/log.h"
#include "utils/Stopwatch.h"
#include "utils/StringUtils.h"
#include "threads/Atomics.h"
#include "windows/GUIWindowPVRCommon.h"
#include "utils/JobManager.h"
#include "interfaces/AnnouncementManager.h"

#include "windows/GUIWindowPVRChannels.h" // HPrasad
#include <fstream> //HPrasad 

#include "PVRManager.h"
#include "PVRDatabase.h"
#include "PVRGUIInfo.h"
#include "addons/PVRClients.h"
//BADJIN 01/03/2016 /////////////////////
#include "addons/PVRClient.h"
/////////////////////////////////////////
#include "channels/PVRChannel.h"
#include "channels/PVRChannelGroupsContainer.h"
#include "channels/PVRChannelGroupInternal.h"
#include "epg/EpgContainer.h"
#include "recordings/PVRRecordings.h"
#include "timers/PVRTimers.h"
#include "interfaces/AnnouncementManager.h"
#include "addons/AddonInstaller.h"
#include "guilib/Key.h"
#include "dialogs/GUIDialogPVRChannelManager.h"

#include <fstream>

using namespace std;
using namespace MUSIC_INFO;
using namespace PVR;
using namespace EPG;
using namespace ANNOUNCEMENT;

CPVRManager::CPVRManager(void) :
  CThread("PVRManager"),
  m_channelGroups(NULL),
  m_recordings(NULL),
  m_timers(NULL),
  m_addons(NULL),
  m_guiInfo(NULL),
  m_triggerEvent(true),
  m_currentFile(NULL),
  m_database(NULL),
  m_bFirstStart(true),
  m_bEpgsCreated(false),
  m_progressHandle(NULL),
  m_managerState(ManagerStateStopped),
  m_bOpenPVRWindow(false),
  m_isShowProgBar(true)
{
  CAnnouncementManager::AddAnnouncer(this);
  ResetProperties();
}

CPVRManager::~CPVRManager(void)
{
  CAnnouncementManager::RemoveAnnouncer(this);
  Stop();
  CLog::Log(LOGDEBUG,"PVRManager - destroyed");
}

void CPVRManager::Announce(AnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
  if (!IsStarted() || (flag & (System)) == 0)
    return;

  if (strcmp(message, "OnWake") == 0)
    ContinueLastChannel();
}

CPVRManager &CPVRManager::Get(void)
{
  static CPVRManager pvrManagerInstance;
  return pvrManagerInstance;
}

void CPVRManager::OnSettingChanged(const CSetting *setting)
{
  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();
  if (settingId == "pvrmanager.enabled")
  {
    if (((CSettingBool*)setting)->GetValue())
      CApplicationMessenger::Get().ExecBuiltIn("XBMC.StartPVRManager", false);
    else
      CApplicationMessenger::Get().ExecBuiltIn("XBMC.StopPVRManager", false);
  }
  else if (settingId == "pvrparental.enabled")
  {
    if (((CSettingBool*)setting)->GetValue() && CSettings::Get().GetString("pvrparental.pin").empty())
    {
      CStdString newPassword = "";
      // password set... save it
      if (CGUIDialogNumeric::ShowAndVerifyNewPassword(newPassword))
        CSettings::Get().SetString("pvrparental.pin", newPassword);
      // password not set... disable parental
      else
        ((CSettingBool*)setting)->SetValue(false);
    }
  }
  //HARSH - TBIRD - ECO - START
  else if (settingId == "selectiptv.playinglist")
  {
    std::string strVal = ((CSettingString*)setting)->GetValue();
    int lanNode = PVR::CGUIWindowPVRChannels::getLanNode(strVal);
    bool isRestart = true;
    if (lanNode >= 0)
    {
      std::string epgUrl   = PVR::CGUIWindowPVRChannels::getEPG(lanNode);
      std::string m3uUrl   = PVR::CGUIWindowPVRChannels::getM3U(lanNode);
      PVR::CGUIWindowPVRChannels::updateIPTVSetting(epgUrl, m3uUrl);
    }

    //Set Current IPTV_LIST
    ofstream CurrentIPTVListFileOut;
    CurrentIPTVListFileOut.open("/storage/.xbmc/userdata/addon_data/service.openelec.settings/CurrentIPTVList.txt");
    CurrentIPTVListFileOut << strVal << "\n";
    CurrentIPTVListFileOut.close();

    std::string IsFirstCurrentIPTVList;
    ifstream IsCurrentIPTVListFileIn("/storage/.xbmc/userdata/addon_data/service.openelec.settings/IsCurrentIPTVList.txt");
    if (IsCurrentIPTVListFileIn.is_open())
    {
      CLog::Log(LOGDEBUG,"PVRManager - HPrasad:Before While");
      while ( getline (IsCurrentIPTVListFileIn,IsFirstCurrentIPTVList) )
      {
        if ( IsFirstCurrentIPTVList.compare("No") == 0 )
        {
          isRestart = false;
        }        
      }
      IsCurrentIPTVListFileIn.close();
    }

    if ( isRestart && g_PVRManager.CheckParentalPIN(g_localizeStrings.Get(19262).c_str()) )
    {
      CGUIDialogOK::ShowAndGetInput(19098, 19186, 750, 0);      
      CDateTime::ResetTimezoneBias();
      g_PVRManager.ResetDatabase(false);
    }
  }
  //HARSH - TBIRD - ECO - FINISH
}

void CPVRManager::OnSettingAction(const CSetting *setting)
{
  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();
  if (settingId == "pvrmenu.searchicons")
  {
    if (IsStarted())
      SearchMissingChannelIcons();
  }
  else if (settingId == "pvrmanager.resetdb")
  {
    if (CheckParentalPIN(g_localizeStrings.Get(19262).c_str()) &&
        CGUIDialogYesNo::ShowAndGetInput(19098, 19186, 750, 0))
    {
      CDateTime::ResetTimezoneBias();
      ResetDatabase(false);
    }
  }
  else if (settingId == "epg.resetepg")
  {
    //(-/+) TBIRD 1.3.6 MODS - START
    if (g_PVRManager.CheckParentalPIN(g_localizeStrings.Get(19262).c_str()) &&
        CGUIDialogYesNo::ShowAndGetInput(19098, 19188, 0, 0))
    {
      CDateTime::ResetTimezoneBias();
      g_PVRManager.ResetDatabase(false);
    }
    /*if (CGUIDialogYesNo::ShowAndGetInput(19098, 19188, 750, 0))
      {
      CDateTime::ResetTimezoneBias();
      g_PVRManager.ResetDatabase(true);
      }*/
    //(-/+) TBIRD 1.3.6 MODS - FINISH
  }
  else if (settingId == "pvrmanager.channelscan")
  {
    if (IsStarted())
      StartChannelScan();
  }
  else if (settingId == "pvrmanager.channelmanager")
  {
    if (IsStarted())
    {
      CGUIDialogPVRChannelManager *dialog = (CGUIDialogPVRChannelManager *)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_CHANNEL_MANAGER);
      if (dialog)
        dialog->DoModal();
    }
  }
  else if (settingId == "pvrclient.menuhook")
  {
    if (IsStarted())
      Clients()->ProcessMenuHooks(-1, PVR_MENUHOOK_SETTING, NULL);
  }
}

bool CPVRManager::IsPVRWindowActive(void) const
{
  return g_windowManager.IsWindowActive(WINDOW_PVR) ||
    g_windowManager.IsWindowActive(WINDOW_DIALOG_PVR_CHANNEL_MANAGER) ||
    g_windowManager.IsWindowActive(WINDOW_DIALOG_PVR_OSD_CHANNELS) ||
    g_windowManager.IsWindowActive(WINDOW_DIALOG_PVR_GROUP_MANAGER) ||
    g_windowManager.IsWindowActive(WINDOW_DIALOG_PVR_GUIDE_INFO) ||
    g_windowManager.IsWindowActive(WINDOW_DIALOG_PVR_OSD_CUTTER) ||
    g_windowManager.IsWindowActive(WINDOW_DIALOG_PVR_OSD_DIRECTOR) ||
    g_windowManager.IsWindowActive(WINDOW_DIALOG_PVR_OSD_GUIDE) ||
    g_windowManager.IsWindowActive(WINDOW_DIALOG_PVR_GUIDE_SEARCH) ||
    g_windowManager.IsWindowActive(WINDOW_DIALOG_PVR_RECORDING_INFO) ||
    g_windowManager.IsWindowActive(WINDOW_DIALOG_PVR_TIMER_SETTING);
}

bool CPVRManager::InstallAddonAllowed(const std::string& strAddonId) const
{
  return !IsStarted() ||
    !m_addons->IsInUse(strAddonId) ||
    (!IsPVRWindowActive() && !IsPlaying());
}

void CPVRManager::MarkAsOutdated(const std::string& strAddonId, const std::string& strReferer)
{
  if (IsStarted() && CSettings::Get().GetBool("general.addonautoupdate"))
  {
    CSingleLock lock(m_critSection);
    m_outdatedAddons.insert(make_pair(strAddonId, strReferer));
  }
}

bool CPVRManager::UpgradeOutdatedAddons(void)
{
  CSingleLock lock(m_critSection);
  if (m_outdatedAddons.empty())
    return true;

  // there's add-ons that couldn't be updated
  for (map<string, string>::iterator it = m_outdatedAddons.begin(); it != m_outdatedAddons.end(); it++)
  {
    if (!InstallAddonAllowed(it->first))
    {
      // we can't upgrade right now
      return true;
    }
  }

  // all outdated add-ons can be upgraded now
  CLog::Log(LOGINFO, "PVR - upgrading outdated add-ons");

  map<string, string> outdatedAddons = m_outdatedAddons;
  // stop threads and unload
  SetState(ManagerStateInterrupted);
  g_EpgContainer.Stop();
  m_guiInfo->Stop();
  m_addons->Stop();
  Cleanup();

  // upgrade all add-ons
  for (map<string, string>::iterator it = outdatedAddons.begin(); it != outdatedAddons.end(); it++)
  {
    CLog::Log(LOGINFO, "PVR - updating add-on '%s'", it->first.c_str());
    CAddonInstaller::Get().Install(it->first, true, it->second, false);
  }

  // reload
  CLog::Log(LOGINFO, "PVRManager - %s - restarting the PVR manager", __FUNCTION__);
  SetState(ManagerStateStarting);
  ResetProperties();

  while (!Load() && IsInitialising())
  {
    CLog::Log(LOGERROR, "PVRManager - %s - failed to load PVR data, retrying", __FUNCTION__);
    if (m_guiInfo) m_guiInfo->Stop();
    if (m_addons) m_addons->Stop();
    Cleanup();
    Sleep(1000);
  }

  if (IsInitialising())
  {
    SetState(ManagerStateStarted);
    g_EpgContainer.Start();

    CLog::Log(LOGDEBUG, "PVRManager - %s - restarted", __FUNCTION__);
    return true;
  }

  return false;
}

void CPVRManager::Cleanup(void)
{
  CSingleLock lock(m_critSection);

  SAFE_DELETE(m_addons);
  SAFE_DELETE(m_guiInfo);
  SAFE_DELETE(m_timers);
  SAFE_DELETE(m_recordings);
  SAFE_DELETE(m_channelGroups);
  SAFE_DELETE(m_parentalTimer);
  SAFE_DELETE(m_database);
  m_triggerEvent.Set();

  m_currentFile           = NULL;
  m_bIsSwitchingChannels  = false;
  m_outdatedAddons.clear();
  m_bOpenPVRWindow = false;
  m_bEpgsCreated = false;

  for (unsigned int iJobPtr = 0; iJobPtr < m_pendingUpdates.size(); iJobPtr++)
    delete m_pendingUpdates.at(iJobPtr);
  m_pendingUpdates.clear();

  HideProgressDialog();

  SetState(ManagerStateStopped);
}

void CPVRManager::ResetProperties(void)
{
  CSingleLock lock(m_critSection);
  Cleanup();

  if (!g_application.m_bStop)
  {
    m_addons        = new CPVRClients;
    m_channelGroups = new CPVRChannelGroupsContainer;
    m_recordings    = new CPVRRecordings;
    m_timers        = new CPVRTimers;
    m_guiInfo       = new CPVRGUIInfo;
    m_parentalTimer = new CStopWatch;
  }
}

class CPVRManagerStartJob : public CJob
{
  public:
    CPVRManagerStartJob(bool bOpenPVRWindow = false) :
      m_bOpenPVRWindow(bOpenPVRWindow) {}
    ~CPVRManagerStartJob(void) {}

    bool DoWork(void)
    {
      g_PVRManager.Start(false, m_bOpenPVRWindow);
      return true;
    }
  private:
    bool m_bOpenPVRWindow;
};

void CPVRManager::Start(bool bAsync /* = false */, bool bOpenPVRWindow /* = false */)
{

  //BADJIN 01/03/2016 /////////////////////
  if (CFile::Exists("/storage/UpdateIPTV.txt")){
    CPVRChannelPtr currentChannel;
    if (GetCurrentChannel(currentChannel)){      
      if (m_addons && m_addons->IsReadingLiveStream()){
        return;
      }
    }
    CFile::Delete("/storage/UpdateIPTV.txt");
  }
  /////////////////////////////////////////

  if (bAsync)
  {
    CPVRManagerStartJob *job = new CPVRManagerStartJob(bOpenPVRWindow);
    CJobManager::GetInstance().AddJob(job, NULL);
    return;
  }

  CSingleLock lock(m_critSection);

  /* first stop and remove any clients */
  Stop();

  /* don't start if Settings->Video->TV->Enable isn't checked */
  if (!CSettings::Get().GetBool("pvrmanager.enabled"))
    return;

  ResetProperties();
  SetState(ManagerStateStarting);
  m_bOpenPVRWindow = bOpenPVRWindow;

  /* create and open database */
  if (!m_database)
    m_database = new CPVRDatabase;
  m_database->Open();

  /* create the supervisor thread to do all background activities */
  StartUpdateThreads();
}

void CPVRManager::Stop(void)
{
  /* check whether the pvrmanager is loaded */
  if (IsStopping() || IsStopped())
    return;

  SetState(ManagerStateStopping);

  /* stop the EPG updater, since it might be using the pvr add-ons */
  g_EpgContainer.Stop();

  CLog::Log(LOGNOTICE, "PVRManager - stopping");

  /* stop playback if needed */
  if (IsPlaying())
  {
    CLog::Log(LOGNOTICE,"PVRManager - %s - stopping PVR playback", __FUNCTION__);
    CApplicationMessenger::Get().MediaStop();
  }

  /* stop all update threads */
  StopUpdateThreads();

  /* executes the set wakeup command */
  SetWakeupCommand();

  /* close database */
  if (m_database->IsOpen())
    m_database->Close();

  /* unload all data */
  Cleanup();
}

ManagerState CPVRManager::GetState(void) const
{
  CSingleLock lock(m_managerStateMutex);
  return m_managerState;
}

void CPVRManager::SetState(ManagerState state) 
{
  {
    CSingleLock lock(m_managerStateMutex);
    m_managerState = state;
    SetChanged();
  }

  NotifyObservers(ObservableMessageManagerStateChanged);
}

void CPVRManager::Process(void)
{
  g_EpgContainer.Stop();

  /* load the pvr data from the db and clients if it's not already loaded */
  while (!Load() && IsInitialising())
  {
    CLog::Log(LOGERROR, "PVRManager - %s - failed to load PVR data, retrying", __FUNCTION__);
    if (m_guiInfo) m_guiInfo->Stop();
    if (m_addons) m_addons->Stop();
    Cleanup();
    Sleep(1000);
  }

  if (IsInitialising())
    SetState(ManagerStateStarted);
  else
    return;

  /* main loop */
  CLog::Log(LOGDEBUG, "PVRManager - %s - entering main loop", __FUNCTION__);
  g_EpgContainer.Start();

  if (m_bOpenPVRWindow)
  {
    m_bOpenPVRWindow = false;
    CApplicationMessenger::Get().ExecBuiltIn("XBMC.ActivateWindowAndFocus(MyPVR, 32,0, 11,0)");
  }

  bool bRestart(false);
  while (IsStarted() && m_addons && m_addons->HasConnectedClients() && !bRestart)
  {
    /* continue last watched channel after first startup */
    if (m_bFirstStart)
    {
      {
        CSingleLock lock(m_critSection);
        m_bFirstStart = false;
      }
      ContinueLastChannel();
    }
    /* execute the next pending jobs if there are any */
    try
    {
      ExecutePendingJobs();
    }
    catch (...)
    {
      CLog::Log(LOGERROR, "PVRManager - %s - an error occured while trying to execute the last update job, trying to recover", __FUNCTION__);
      bRestart = true;
    }

    if (!UpgradeOutdatedAddons())
    {
      // failed to load after upgrading
      CLog::Log(LOGERROR, "PVRManager - %s - could not load pvr data after upgrading. stopping the pvrmanager", __FUNCTION__);
    }
    else if (IsStarted() && !bRestart)
      m_triggerEvent.WaitMSec(1000);
  }

  if (IsStarted())
  {
    CLog::Log(LOGNOTICE, "PVRManager - %s - no add-ons enabled anymore. restarting the pvrmanager", __FUNCTION__);
    CApplicationMessenger::Get().ExecBuiltIn("StartPVRManager", false);
  }
  else
  {
    if (g_windowManager.GetActiveWindow() == WINDOW_PVR)
      g_windowManager.ActivateWindow(WINDOW_HOME);
  }
}

bool CPVRManager::SetWakeupCommand(void)
{
  if (!CSettings::Get().GetBool("pvrpowermanagement.enabled"))
    return false;

  const CStdString strWakeupCommand = CSettings::Get().GetString("pvrpowermanagement.setwakeupcmd");
  if (!strWakeupCommand.empty() && m_timers)
  {
    time_t iWakeupTime;
    const CDateTime nextEvent = m_timers->GetNextEventTime();
    if (nextEvent.IsValid())
    {
      nextEvent.GetAsTime(iWakeupTime);

      CStdString strExecCommand = StringUtils::Format("%s %d", strWakeupCommand.c_str(), iWakeupTime);

      const int iReturn = system(strExecCommand.c_str());
      if (iReturn != 0)
        CLog::Log(LOGERROR, "%s - failed to execute wakeup command '%s': %s (%d)", __FUNCTION__, strExecCommand.c_str(), strerror(iReturn), iReturn);

      return iReturn == 0;
    }
  }

  return false;
}

bool CPVRManager::StartUpdateThreads(void)
{
  StopUpdateThreads();
  CLog::Log(LOGNOTICE, "PVRManager - starting up");

  /* create the pvrmanager thread, which will ensure that all data will be loaded */
  SetState(ManagerStateStarting);
  Create();
  SetPriority(-1);

  return true;
}

void CPVRManager::StopUpdateThreads(void)
{
  SetState(ManagerStateInterrupted);

  StopThread();
  if (m_guiInfo)
    m_guiInfo->Stop();
  if (m_addons){
    m_addons->Stop();
  }
}

bool CPVRManager::Load(void)
{
  /* start the add-on update thread */
  if (m_addons)
    m_addons->Start();

  /* load at least one client */
  while (IsInitialising() && m_addons && !m_addons->HasConnectedAllClients())
    Sleep(50);

  if (!IsInitialising() || !m_addons || !m_addons->HasConnectedClients())
    return false;

  CLog::Log(LOGDEBUG, "PVRManager - %s - active clients found. continue to start", __FUNCTION__);

  CGUIWindowPVR *pWindow = (CGUIWindowPVR *) g_windowManager.GetWindow(WINDOW_PVR);
  if (pWindow)
    pWindow->Reset();

  CLog::Log(LOGDEBUG, "PVRManager - %s - Show Progress Bar ? - %s", __FUNCTION__, m_isShowProgBar ? "true" : "false");

  static bool firstTime = true;
  if(firstTime)
  {
    CLog::Log(LOGDEBUG, "PVRManager - %s - First Time Delay for [Loading channels from clients....] - Start", __FUNCTION__);
    while(true != g_application.isStartupVideoDone())
      Sleep(500);
    CLog::Log(LOGDEBUG, "PVRManager - %s - First Time Delay for [Loading channels from clients....] - End", __FUNCTION__);
    firstTime = false;
  }

  /* load all channels and groups */
  if( m_isShowProgBar == true ){
    //(+/-) Dulanga pvr progress bar illusion - Start
    for(int i = 0; i < 10; i++)
    {
      ShowProgressDialog(g_localizeStrings.Get(19236), i); // Loading channels from clients
      Sleep(20);
    }
    //ShowProgressDialog(g_localizeStrings.Get(19236), 0); // Loading channels from clients
    //(+/-) Dulanga pvr progress bar illusion - End
  }
  if (!m_channelGroups->Load() || !IsInitialising())
    return false;

  /* get timers from the backends */
  if( m_isShowProgBar == true ){
    //(+) Dulanga pvr progress bar illusion - Start
    int channelGroupsSize = m_channelGroups->Get(false)->Size();
    CLog::Log(LOGDEBUG, "PVRManager -Loaded channel group size %d - ", channelGroupsSize);
    if (channelGroupsSize > 1) //1 is shown when there is no internet
    {
      for(int i = 25; i < 50; i++)
      {
        ShowProgressDialog(g_localizeStrings.Get(19236), i); // Loading channels from clients
        Sleep(20);
      }
    }
    //(+) Dulanga pvr progress bar illusion - End
    ShowProgressDialog(g_localizeStrings.Get(19237), 50); // Loading timers from clients
  }
  m_timers->Load();

  /* get recordings from the backend */
  if( m_isShowProgBar == true ){
    ShowProgressDialog(g_localizeStrings.Get(19238), 75); // Loading recordings from clients
  }
  m_recordings->Load();

  if (!IsInitialising())
    return false;

  /* start the other pvr related update threads */
  if( m_isShowProgBar == true ){
    ShowProgressDialog(g_localizeStrings.Get(19239), 85); // Starting background threads
  }
  m_guiInfo->Start();

  /* close the progess dialog */
  HideProgressDialog();

  return true;
}

void CPVRManager::ShowProgressDialog(const CStdString &strText, int iProgress)
{
  if (!m_progressHandle)
  {
    CGUIDialogExtendedProgressBar *loadingProgressDialog = (CGUIDialogExtendedProgressBar *)g_windowManager.GetWindow(WINDOW_DIALOG_EXT_PROGRESS);
    m_progressHandle = loadingProgressDialog->GetHandle(g_localizeStrings.Get(19235)); // PVR manager is starting up
  }

  m_progressHandle->SetPercentage((float)iProgress);
  m_progressHandle->SetText(strText);
}

void CPVRManager::HideProgressDialog(void)
{
  if (m_progressHandle)
  {
    m_progressHandle->MarkFinished();
    m_progressHandle = NULL;
  }
}

bool CPVRManager::ChannelSwitch(unsigned int iChannelNumber)
{
  CSingleLock lock(m_critSection);

  CPVRChannelGroupPtr playingGroup = GetPlayingGroup(m_addons->IsPlayingRadio());
  if (!playingGroup)
  {
    CLog::Log(LOGERROR, "PVRManager - %s - cannot get the selected group", __FUNCTION__);
    return false;
  }

  CFileItemPtr channel = playingGroup->GetByChannelNumber(iChannelNumber);
  if (!channel || !channel->HasPVRChannelInfoTag())
  {
    CLog::Log(LOGERROR, "PVRManager - %s - cannot find channel %d", __FUNCTION__, iChannelNumber);
    return false;
  }

  return PerformChannelSwitch(*channel->GetPVRChannelInfoTag(), false);
}

bool CPVRManager::ChannelUpDown(unsigned int *iNewChannelNumber, bool bPreview, bool bUp)
{
  bool bReturn = false;
  if (IsPlayingTV() || IsPlayingRadio())
  {
    CFileItem currentFile(g_application.CurrentFileItem());
    CPVRChannel *currentChannel = currentFile.GetPVRChannelInfoTag();
    CPVRChannelGroupPtr group = GetPlayingGroup(currentChannel->IsRadio());
    if (group)
    {
      CFileItemPtr newChannel = bUp ?
        group->GetByChannelUp(*currentChannel) :
        group->GetByChannelDown(*currentChannel);

      if (newChannel && newChannel->HasPVRChannelInfoTag() &&
          PerformChannelSwitch(*newChannel->GetPVRChannelInfoTag(), bPreview))
      {
        *iNewChannelNumber = newChannel->GetPVRChannelInfoTag()->ChannelNumber();
        bReturn = true;
      }
    }
  }

  return bReturn;
}

bool CPVRManager::ContinueLastChannel(void)
{
  if (CSettings::Get().GetInt("pvrplayback.startlast") == START_LAST_CHANNEL_OFF)
    return false;

  CFileItemPtr channel = m_channelGroups->GetLastPlayedChannel();
  if (channel && channel->HasPVRChannelInfoTag())
  {
    CLog::Log(LOGNOTICE, "PVRManager - %s - continue playback on channel '%s'", __FUNCTION__, channel->GetPVRChannelInfoTag()->ChannelName().c_str());
    return StartPlayback(channel->GetPVRChannelInfoTag(), (CSettings::Get().GetInt("pvrplayback.startlast") == START_LAST_CHANNEL_MIN));
  }

  return false;
}

void CPVRManager::ResetDatabase(bool bResetEPGOnly /* = false */, bool showProgBar /* = true */)
{
  CLog::Log(LOGNOTICE,"PVRManager - %s - clearing the PVR database - Show Progress Bar? - %s", __FUNCTION__, showProgBar ? "true" : "false");

  //BADJIN 14/12/2015 /////////////////////
  if (CFile::Exists("/storage/.xbmc/userdata/addon_data/pvr.iptvsimple/iptv.m3u.cache"))
    CFile::Delete("/storage/.xbmc/userdata/addon_data/pvr.iptvsimple/iptv.m3u.cache");
  /////////////////////////////////////////

  g_EpgContainer.Stop();
  if( showProgBar==false ){
    m_isShowProgBar = false;      
  }

  CGUIDialogProgress* pDlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
  if(showProgBar){
    pDlgProgress->SetHeading("Reset Database");
    pDlgProgress->SetLine(0, StringUtils::EmptyString);
    pDlgProgress->SetLine(1, g_localizeStrings.Get(19186)); // All data in the PVR database is being erased
    pDlgProgress->SetLine(2, StringUtils::EmptyString);
    pDlgProgress->StartModal();
    pDlgProgress->Progress();
  }

  if (m_addons && m_addons->IsPlaying())
  {
    CLog::Log(LOGNOTICE,"PVRManager - %s - stopping playback", __FUNCTION__);
    CApplicationMessenger::Get().MediaStop();
  }

  if(showProgBar){
    pDlgProgress->SetPercentage(10);
    pDlgProgress->Progress();
  }

  /* reset the EPG pointers */
  if (m_database)
    m_database->ResetEPG();

  /* stop the thread */
  Stop();

  if(showProgBar){
    pDlgProgress->SetPercentage(20);
    pDlgProgress->Progress();
  }

  if (!m_database)
    m_database = new CPVRDatabase;

  if (m_database && m_database->Open())
  {
    /* clean the EPG database */
    g_EpgContainer.Reset();
    if(showProgBar){
      pDlgProgress->SetPercentage(30);
      pDlgProgress->Progress();
    }

    if (!bResetEPGOnly)
    {
      m_database->DeleteChannelGroups();
      if(showProgBar){
        pDlgProgress->SetPercentage(50);
        pDlgProgress->Progress();
      }

      /* delete all channels */
      m_database->DeleteChannels();
      if(showProgBar){
        pDlgProgress->SetPercentage(70);
        pDlgProgress->Progress();
      }

      /* delete all channel settings */
      m_database->DeleteChannelSettings();
      if(showProgBar){
        pDlgProgress->SetPercentage(80);
        pDlgProgress->Progress();
      }

      /* delete all client information */
      m_database->DeleteClients();
      if(showProgBar){
        pDlgProgress->SetPercentage(90);
        pDlgProgress->Progress();
      }
    }

    m_database->Close();
  }

  CLog::Log(LOGNOTICE,"PVRManager - %s - %s database cleared", __FUNCTION__, bResetEPGOnly ? "EPG" : "PVR and EPG");

  if (CSettings::Get().GetBool("pvrmanager.enabled"))
  {
    CLog::Log(LOGNOTICE,"PVRManager - %s - restarting the PVRManager", __FUNCTION__);
    m_database->Open();
    Cleanup();
    Start();
  }

  if(showProgBar){
    pDlgProgress->SetPercentage(100);
    pDlgProgress->Close();
  }
}

//(+)TBIRD - ECO - START
void CPVRManager::SilentDatabaseReset(bool bResetEPGOnly /* = false */)
{
  g_EpgContainer.Stop();

  if (m_addons && m_addons->IsPlaying())
  {    
    CApplicationMessenger::Get().MediaStop();
  }

  /* reset the EPG pointers */
  if (m_database)
    m_database->ResetEPG();

  /* stop the thread */
  Stop();

  if (!m_database)
    m_database = new CPVRDatabase;

  if (m_database && m_database->Open())
  {
    /* clean the EPG database */
    g_EpgContainer.Reset();

    if (!bResetEPGOnly)
    {
      m_database->DeleteChannelGroups();

      /* delete all channels */
      m_database->DeleteChannels();

      /* delete all channel settings */
      m_database->DeleteChannelSettings();     

      /* delete all client information */
      m_database->DeleteClients();     
    }

    m_database->Close();
  }

  HideProgressDialog();

  if (CSettings::Get().GetBool("pvrmanager.enabled"))
  {    
    m_database->Open();
    Cleanup();
    Start();
    HideProgressDialog();
    //std::string command = "/storage/.config/channelcheck.py";
    //std::string command = "awk \'BEGIN{FS=\";\"} !x[$1]++\' /storage/.xbmc/userdata/addon_data/service.multimedia.vdr-addon/config/Allchannels.conf > /storage/.xbmc/userdata/addon_data/service.multimedia.vdr-addon/config/channels.conf";
    //system(command.c_str());
  }

}
//(+)TBIRD - ECO - FINISH

bool CPVRManager::IsPlaying(void) const
{
  return IsStarted() && m_addons && m_addons->IsPlaying();
}

bool CPVRManager::GetCurrentChannel(CPVRChannelPtr &channel) const
{
  return m_addons && m_addons->GetPlayingChannel(channel);
}

int CPVRManager::GetCurrentEpg(CFileItemList &results) const
{
  int iReturn = -1;

  CPVRChannelPtr channel;
  if (m_addons->GetPlayingChannel(channel))
    iReturn = channel->GetEPG(results);
  else
    CLog::Log(LOGDEBUG,"PVRManager - %s - no current channel set", __FUNCTION__);

  return iReturn;
}

void CPVRManager::ResetPlayingTag(void)
{
  CSingleLock lock(m_critSection);
  if (IsStarted() && m_guiInfo)
    m_guiInfo->ResetPlayingTag();
}

int CPVRManager::GetPreviousChannel(void)
{
  CPVRChannelPtr currentChannel;
  if (GetCurrentChannel(currentChannel))
  {
    CPVRChannelGroupPtr selectedGroup = GetPlayingGroup(currentChannel->IsRadio());
    CFileItemPtr channel = selectedGroup->GetLastPlayedChannel(currentChannel->ChannelID());
    if (channel && channel->HasPVRChannelInfoTag())
      return channel->GetPVRChannelInfoTag()->ChannelNumber();
  }
  return -1;
}

// bool CPVRManager::ToggleRecordingOnChannel(unsigned int iChannelId)
// {
//   bool bReturn = false;
//
//   CPVRChannelPtr channel = m_channelGroups->GetChannelById(iChannelId);
//   if (!channel)
//     return bReturn;
//
//   if (m_addons->HasTimerSupport(channel->ClientID()))
//   {
//     CLog::Log(LOGDEBUG,"***********************************************************************Channel %d is clicked, start processing", iChannelId);
//     if ( m_recordingMap.find(iChannelId) == m_recordingMap.end() )
//     {
//       CLog::Log(LOGDEBUG,"***********************************************************************Channel %d is NEW, start processing to insert", iChannelId);
//       std::pair<std::map<int, int>::iterator,bool> ret;
//       ret = m_recordingMap.insert( std::pair<int,int>(iChannelId,0) );
//       if (ret.second==false) {
//         CLog::Log(LOGDEBUG,"**************************Channel %d is already in the recording map,insertion failed. old  Value is:%d", iChannelId, ret.first->second);
//       }
//       else
//       {
//         m_recordingMap[iChannelId]++;
//       }
//     }
//     else 
//     {
//       m_recordingMap[iChannelId]++;
//       CLog::Log(LOGDEBUG,"**************************Channel %d is inserted in the recording map,inserted  Value is:%d", iChannelId, m_recordingMap.find(iChannelId)->second);
//     }
//     CLog::Log(LOGDEBUG,"~~~~~~~~~~~~~~~~~~~~~~~~~~after processing Channel %d Value is:%d", iChannelId, m_recordingMap.find(iChannelId)->second);
//
//
//
//     #<{(| timers are supported on this channel |)}>#
//     if (!channel->IsRecording())
//     {
//       if((m_recordingMap.find(iChannelId)->second) < 2)
//       {
//         //(-/+) TBIRD 1.3.6 MODS - START
//         CLog::Log(LOGDEBUG,"********** Channel is not Recording at the moment! **********");
//         bReturn = m_timers->InstantTimer(*channel);      
//         //(-/+) TBIRD 1.3.6 MODS - FINISH
//         if (!bReturn)
//           CGUIDialogOK::ShowAndGetInput(19033,0,19164,0);
//       }
//       else 
//       {
//         bReturn = m_timers->DeleteTimersOnChannel(*channel,true, false);
//         CLog::Log(LOGDEBUG,"*********************************************************************bReturn in OUR delete:%d",bReturn);
//         CLog::Log(LOGDEBUG,"********** Channel is not Recording at the moment! **********");
//         std::map<int,int>::iterator it;
//         it = m_recordingMap.find(iChannelId);
//         m_recordingMap.erase(it);
//         //Send the message to inform the user that we stopped recording:
//         CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error,
//             g_localizeStrings.Get(19166), 
//             g_localizeStrings.Get(19256));
//       }
//     }
//     else
//     {
//       std::map<int,int>::iterator it;
//       it = m_recordingMap.find(iChannelId);
//       m_recordingMap.erase(it);
//       //TBIRD 1.3.6 MODS - START
//       CLog::Log(LOGDEBUG,"############ Channel is Recording at the moment! ############");
//       //TBIRD 1.3.6 MODS - FINISH
//       #<{(| delete active timers |)}>#
//       bReturn = m_timers->DeleteTimersOnChannel(*channel, true, false);
//       CLog::Log(LOGDEBUG,"*********************************************************************bReturn in normal delete:%d",bReturn);
//       //TBIRD 1.3.4 MODS - START
//       //Send the message to inform the user that we stopped recording:
//       CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error,
//           g_localizeStrings.Get(19166), 
//           g_localizeStrings.Get(19256));
//       //TBIRD 1.3.4 MODS - FINISH
//     }
//   }
//
//   std::map<int,int>::iterator it;
//   for (it=m_recordingMap.begin(); it!=m_recordingMap.end(); ++it)
//     CLog::Log(LOGDEBUG,"###### CONTENT IN MAP ###### key:%d Value:%d",it->first, it->second);
//
//   return bReturn;
// }

bool CPVRManager::ToggleRecordingOnChannel(unsigned int iChannelId)
{
  for (map<CDateTime, vector<CPVRTimerInfoTagPtr>* >::const_iterator it =m_timers->m_tags.begin(); it != m_timers->m_tags.end(); it++)
  {
      CLog::Log(LOGDEBUG,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ PVRManager tag key:%d", it->first.GetSecond());
    for (vector<CPVRTimerInfoTagPtr>::const_iterator timerIt = it->second->begin(); timerIt != it->second->end(); timerIt++)
    {
      CLog::Log(LOGDEBUG,"...................data in PVRManager.cpp............timer:%d clientId:%d ubiqueTimerId:%d startTime:%d", 
          (*timerIt)->m_iClientIndex, (*timerIt)->m_iClientId,(*timerIt)->m_iTimerId, (*timerIt)->m_StartTime.GetSecond());
    }
  }
  bool bReturn = false;

  CPVRChannelPtr channel = m_channelGroups->GetChannelById(iChannelId);
  if (!channel)
    return bReturn;

  if (m_addons->HasTimerSupport(channel->ClientID()))
  {
    CLog::Log(LOGDEBUG,"***********************************************************************Channel %d is clicked, start processing", iChannelId);
    if ( m_recordingMap.find(iChannelId) == m_recordingMap.end() )
    {
      CLog::Log(LOGDEBUG,"***********************************************************************Channel %d is NEW, start processing to insert", iChannelId);
      std::pair<std::map<int, int>::iterator,bool> ret;
      ret = m_recordingMap.insert( std::pair<int,int>(iChannelId,0) );
      if (ret.second==false) {
        CLog::Log(LOGDEBUG,"**************************Channel %d is already in the recording map,insertion failed. old  Value is:%d", iChannelId, ret.first->second);
      }
      else
      {
        m_recordingMap[iChannelId]++;
      }
    }
    else 
    {
      m_recordingMap[iChannelId]++;
      CLog::Log(LOGDEBUG,"**************************Channel %d is inserted in the recording map,inserted  Value is:%d", iChannelId, m_recordingMap.find(iChannelId)->second);
    }
    CLog::Log(LOGDEBUG,"~~~~~~~~~~~~~~~~~~~~~~~~~~after processing Channel %d Value is:%d", iChannelId, m_recordingMap.find(iChannelId)->second);



    /* timers are supported on this channel */
    if (!channel->IsRecording())
    {
      if((m_recordingMap.find(iChannelId)->second) < 2)
      {
        //(-/+) TBIRD 1.3.6 MODS - START
        CLog::Log(LOGDEBUG,"********** Channel is not Recording at the moment! **********");
        bReturn = m_timers->InstantTimer(*channel);      
        //(-/+) TBIRD 1.3.6 MODS - FINISH
        if (!bReturn)
          CGUIDialogOK::ShowAndGetInput(19033,0,19164,0);
      }
      else 
      {
        bReturn = m_timers->DeleteTimersOnChannel(*channel,true, false);
        CLog::Log(LOGDEBUG,"*********************************************************************bReturn in OUR delete:%d",bReturn);
        CLog::Log(LOGDEBUG,"********** Channel is not Recording at the moment! **********");
        std::map<int,int>::iterator it;
        it = m_recordingMap.find(iChannelId);
        m_recordingMap.erase(it);
        //Send the message to inform the user that we stopped recording:
        CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error,
            g_localizeStrings.Get(19166), 
            g_localizeStrings.Get(19256));
      }
    }
    else
    {
      std::map<int,int>::iterator it;
      it = m_recordingMap.find(iChannelId);
      m_recordingMap.erase(it);
      //TBIRD 1.3.6 MODS - START
      CLog::Log(LOGDEBUG,"############ Channel is Recording at the moment! ############");
      //TBIRD 1.3.6 MODS - FINISH
      /* delete active timers */
      bReturn = m_timers->DeleteTimersOnChannel(*channel,false, true);
      CLog::Log(LOGDEBUG,"*********************************************************************bReturn in normal delete:%d",bReturn);
      //TBIRD 1.3.4 MODS - START
      //Send the message to inform the user that we stopped recording:
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error,
          g_localizeStrings.Get(19166), 
          g_localizeStrings.Get(19256));
      //TBIRD 1.3.4 MODS - FINISH
    }
  }

  std::map<int,int>::iterator it;
  for (it=m_recordingMap.begin(); it!=m_recordingMap.end(); ++it)
    CLog::Log(LOGDEBUG,"###### CONTENT IN MAP ###### key:%d Value:%d",it->first, it->second);

  for (map<CDateTime, vector<CPVRTimerInfoTagPtr>* >::const_iterator it =m_timers->m_tags.begin(); it != m_timers->m_tags.end(); it++)
  {
      CLog::Log(LOGDEBUG,"~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
    for (vector<CPVRTimerInfoTagPtr>::const_iterator timerIt = it->second->begin(); timerIt != it->second->end(); timerIt++)
    {
      CLog::Log(LOGDEBUG,"...................data in PVRManager.cpp............timer:%d clientId:%d ubiqueTimerId:%d startTime:%d", 
          (*timerIt)->m_iClientIndex, (*timerIt)->m_iClientId,(*timerIt)->m_iTimerId, (*timerIt)->m_StartTime.GetSecond());
    }
  }
  return bReturn;
}

bool CPVRManager::StartRecordingOnPlayingChannel(bool bOnOff)
{
  bool bReturn = false;

  CPVRChannelPtr channel;
  if (!m_addons->GetPlayingChannel(channel))
    return bReturn;

  if (m_addons->HasTimerSupport(channel->ClientID()))
  {
    /* timers are supported on this channel */
    if (bOnOff && !channel->IsRecording())
    {
      bReturn = m_timers->InstantTimer(*channel);
      if (!bReturn)
        CGUIDialogOK::ShowAndGetInput(19033,0,19164,0);
    }
    else if (!bOnOff && channel->IsRecording())
    {
      /* delete active timers */
      bReturn = m_timers->DeleteTimersOnChannel(*channel, true, true);
    }
  }

  return bReturn;
}

bool CPVRManager::CheckParentalLock(const CPVRChannel &channel)
{
  bool bReturn = !IsParentalLocked(channel) ||
    CheckParentalPIN();

  if (!bReturn)
    CLog::Log(LOGERROR, "PVRManager - %s - parental lock verification failed for channel '%s': wrong PIN entered.", __FUNCTION__, channel.ChannelName().c_str());

  return bReturn;
}

bool CPVRManager::IsParentalLocked(const CPVRChannel &channel)
{
  bool bReturn(false);
  CSingleLock lock(m_managerStateMutex);
  if (!IsStarted())
    return bReturn;
  CPVRChannelPtr currentChannel(new CPVRChannel(false));

  if (// different channel
      (!GetCurrentChannel(currentChannel) || channel != *currentChannel) &&
      // parental control enabled
      CSettings::Get().GetBool("pvrparental.enabled") &&
      // channel is locked
      channel.IsLocked())
  {
    float parentalDurationMs = CSettings::Get().GetInt("pvrparental.duration") * 1000.0f;
    bReturn = m_parentalTimer &&
      (!m_parentalTimer->IsRunning() ||
       m_parentalTimer->GetElapsedMilliseconds() > parentalDurationMs);
  }

  return bReturn;
}

bool CPVRManager::CheckParentalPIN(const char *strTitle /* = NULL */)
{
  CStdString pinCode = CSettings::Get().GetString("pvrparental.pin");

  if (!CSettings::Get().GetBool("pvrparental.enabled") || pinCode.empty())
    return true;

  // Locked channel. Enter PIN:
  bool bValidPIN = CGUIDialogNumeric::ShowAndVerifyInput(pinCode, strTitle ? strTitle : g_localizeStrings.Get(19263).c_str(), true);
  if (!bValidPIN)
    // display message: The entered PIN number was incorrect
    CGUIDialogOK::ShowAndGetInput(19264,0,19265,0);
  else if (m_parentalTimer)
  {
    // reset the timer
    m_parentalTimer->StartZero();
  }

  return bValidPIN;
}

void CPVRManager::SaveCurrentChannelSettings(void)
{
  m_addons->SaveCurrentChannelSettings();
}

void CPVRManager::LoadCurrentChannelSettings()
{
  m_addons->LoadCurrentChannelSettings();
}

void CPVRManager::SetPlayingGroup(CPVRChannelGroupPtr group)
{
  if (m_channelGroups && group)
    m_channelGroups->Get(group->IsRadio())->SetSelectedGroup(group);
}

CPVRChannelGroupPtr CPVRManager::GetPlayingGroup(bool bRadio /* = false */)
{
  if (m_channelGroups)
    return m_channelGroups->GetSelectedGroup(bRadio);

  return CPVRChannelGroupPtr();
}

bool CPVREpgsCreateJob::DoWork(void)
{
  return g_PVRManager.CreateChannelEpgs();
}

bool CPVRRecordingsUpdateJob::DoWork(void)
{
  g_PVRRecordings->Update();
  return true;
}

bool CPVRTimersUpdateJob::DoWork(void)
{
  return g_PVRTimers->Update();
}

bool CPVRChannelsUpdateJob::DoWork(void)
{
  return g_PVRChannelGroups->Update(true);
}

bool CPVRChannelGroupsUpdateJob::DoWork(void)
{
  return g_PVRChannelGroups->Update(false);
}

bool CPVRChannelSettingsSaveJob::DoWork(void)
{
  g_PVRManager.SaveCurrentChannelSettings();
  return true;
}

bool CPVRManager::OpenLiveStream(const CFileItem &channel)
{
  bool bReturn(false);
  if (!channel.HasPVRChannelInfoTag())
    return bReturn;

  CLog::Log(LOGDEBUG,"PVRManager - %s - opening live stream on channel '%s'",
      __FUNCTION__, channel.GetPVRChannelInfoTag()->ChannelName().c_str());

  // check if we're allowed to play this file
  if (IsParentalLocked(*channel.GetPVRChannelInfoTag()))
    return bReturn;
  ////////////////BADJIN 08/03/2016////////////////////////////////////
  if (CFile::Exists("/storage/UpdateIPTV.txt") && g_windowManager.GetActiveWindow() == WINDOW_PVR) { 
    CFile::Rename("/storage/UpdateIPTV.txt","/storage/HoldUpdateIPTV.txt");
    CStdString clientName;
    m_addons->GetClientName(channel.GetPVRChannelInfoTag()->ClientID(), clientName);    
    if (strstr(clientName.c_str(),"IPTV") != NULL) {
      if (CGUIDialogYesNo::ShowAndGetInput(19286,19287,19288,0)) {
        CFile::Delete("/storage/HoldUpdateIPTV.txt");
        Start();
        return bReturn;
      }
      else CFile::Rename("/storage/HoldUpdateIPTV.txt","/storage/UpdateIPTV.txt");
    }
    else CFile::Rename("/storage/HoldUpdateIPTV.txt","/storage/UpdateIPTV.txt");
  }
  /////////////////////////////////////////////////////////////////////
  CPVRChannelPtr playingChannel;
  bool bPersistChannel(false);
  if ((bReturn = m_addons->OpenStream(*channel.GetPVRChannelInfoTag(), false)) != false)
  {
    CSingleLock lock(m_critSection);
    if(m_currentFile)
      delete m_currentFile;
    m_currentFile = new CFileItem(channel);

    if (m_addons->GetPlayingChannel(playingChannel))
    {
      /* store current time in iLastWatched */
      time_t tNow;
      CDateTime::GetCurrentDateTime().GetAsTime(tNow);
      playingChannel->SetLastWatched(tNow);
      bPersistChannel = true;

      m_channelGroups->SetLastPlayedGroup(GetPlayingGroup(playingChannel->IsRadio()));
    }
  }

  if (bPersistChannel)
    playingChannel->Persist();

  return bReturn;
}

bool CPVRManager::OpenRecordedStream(const CPVRRecording &tag)
{
  bool bReturn = false;
  CSingleLock lock(m_critSection);

  if ((bReturn = m_addons->OpenStream(tag)) != false)
  {
    delete m_currentFile;
    m_currentFile = new CFileItem(tag);
  }

  return bReturn;
}

void CPVRManager::CloseStream(void)
{
  CPVRChannelPtr channel;
  bool bPersistChannel(false);

  {
    CSingleLock lock(m_critSection);

    if (m_addons->GetPlayingChannel(channel))
    {
      /* store current time in iLastWatched */
      time_t tNow;
      CDateTime::GetCurrentDateTime().GetAsTime(tNow);
      channel->SetLastWatched(tNow);
      bPersistChannel = true;

      m_channelGroups->SetLastPlayedGroup(GetPlayingGroup(channel->IsRadio()));
    }

    m_addons->CloseStream();
    SAFE_DELETE(m_currentFile);
  }

  if (bPersistChannel)
    channel->Persist();  
}

void CPVRManager::UpdateCurrentFile(void)
{
  CSingleLock lock(m_critSection);
  if (m_currentFile)
    UpdateItem(*m_currentFile);
}

bool CPVRManager::UpdateItem(CFileItem& item)
{
  /* Don't update if a recording is played */
  if (item.IsPVRRecording())
    return false;

  if (!item.IsPVRChannel())
  {
    CLog::Log(LOGERROR, "CPVRManager - %s - no channel tag provided", __FUNCTION__);
    return false;
  }

  CSingleLock lock(m_critSection);
  if (!m_currentFile || *m_currentFile->GetPVRChannelInfoTag() == *item.GetPVRChannelInfoTag())
    return false;

  g_application.CurrentFileItem() = *m_currentFile;
  g_infoManager.SetCurrentItem(*m_currentFile);

  CPVRChannel* channelTag = item.GetPVRChannelInfoTag();
  CEpgInfoTag epgTagNow;
  bool bHasTagNow = channelTag->GetEPGNow(epgTagNow);

  if (channelTag->IsRadio())
  {
    CMusicInfoTag* musictag = item.GetMusicInfoTag();
    if (musictag)
    {
      musictag->SetTitle(bHasTagNow ?
          epgTagNow.Title() :
          CSettings::Get().GetBool("epg.hidenoinfoavailable") ?
          StringUtils::EmptyString :
          g_localizeStrings.Get(19055)); // no information available
      if (bHasTagNow)
        musictag->SetGenre(epgTagNow.Genre());
      musictag->SetDuration(bHasTagNow ? epgTagNow.GetDuration() : 3600);
      musictag->SetURL(channelTag->Path());
      musictag->SetArtist(channelTag->ChannelName());
      musictag->SetAlbumArtist(channelTag->ChannelName());
      musictag->SetLoaded(true);
      musictag->SetComment(StringUtils::EmptyString);
      musictag->SetLyrics(StringUtils::EmptyString);
    }
  }
  else
  {
    CVideoInfoTag *videotag = item.GetVideoInfoTag();
    if (videotag)
    {
      videotag->m_strTitle = bHasTagNow ?
        epgTagNow.Title() :
        CSettings::Get().GetBool("epg.hidenoinfoavailable") ?
        StringUtils::EmptyString :
        g_localizeStrings.Get(19055); // no information available
      if (bHasTagNow)
        videotag->m_genre = epgTagNow.Genre();
      videotag->m_strPath = channelTag->Path();
      videotag->m_strFileNameAndPath = channelTag->Path();
      videotag->m_strPlot = bHasTagNow ? epgTagNow.Plot() : StringUtils::EmptyString;
      videotag->m_strPlotOutline = bHasTagNow ? epgTagNow.PlotOutline() : StringUtils::EmptyString;
      videotag->m_iEpisode = bHasTagNow ? epgTagNow.EpisodeNum() : 0;
    }
  }

  return false;
}

bool CPVRManager::StartPlayback(const CPVRChannel *channel, bool bPreview /* = false */)
{
  CMediaSettings::Get().SetVideoStartWindowed(bPreview);
  CApplicationMessenger::Get().MediaPlay(CFileItem(*channel));
  CLog::Log(LOGNOTICE, "PVRManager - %s - started playback on channel '%s'",
      __FUNCTION__, channel->ChannelName().c_str());
  return true;
}

bool CPVRManager::StartPlayback(PlaybackType type /* = PlaybackTypeAny */)
{
  bool bIsRadio(false);
  bool bReturn(false);
  bool bIsPlaying(false);
  CFileItemPtr channel;

  // check if the desired PlaybackType is already playing,
  // and if not, try to grab the last played channel of this type
  switch (type)
  {
  case PlaybackTypeRadio:
    if (IsPlayingRadio())
      bIsPlaying = true;
    else
      channel = m_channelGroups->GetGroupAllRadio()->GetLastPlayedChannel();
    bIsRadio = true;
    break;

  case PlaybackTypeTv:
    if (IsPlayingTV())
      bIsPlaying = true;
    else
      channel = m_channelGroups->GetGroupAllTV()->GetLastPlayedChannel();
    break;

  default:
    if (IsPlaying())
      bIsPlaying = true;
    else
      channel = m_channelGroups->GetLastPlayedChannel();
  }

  // we're already playing? Then nothing to do
  if (bIsPlaying)
    return true;

  // if we have a last played channel, start playback
  if (channel && channel->HasPVRChannelInfoTag())
  {
    bReturn = StartPlayback(channel->GetPVRChannelInfoTag(), false);
  }
  else
  {
    // if we don't, find the active channel group of the demanded type and play it's first channel
    CPVRChannelGroupPtr channelGroup = GetPlayingGroup(bIsRadio);
    if (channelGroup)
    {
      // try to start playback of first channel in this group
      CFileItemPtr channel = channelGroup->GetByIndex(0);
      if (channel && channel->HasPVRChannelInfoTag())
        bReturn = StartPlayback(channel->GetPVRChannelInfoTag(), false);
    }
  }

  if (!bReturn)
  {
    CLog::Log(LOGNOTICE, "PVRManager - %s - could not determine %s channel to start playback with. No last played channel found, and first channel of active group could also not be determined.", __FUNCTION__, bIsRadio ? "radio": "tv");

    CStdString msg = StringUtils::Format(g_localizeStrings.Get(19035).c_str(), g_localizeStrings.Get(bIsRadio ? 19021 : 19020).c_str()); // RADIO/TV could not be played. Check the log for details.
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error,
        g_localizeStrings.Get(19166), // PVR information
        msg);
  }

  return bReturn;
}


bool CPVRManager::PerformChannelSwitch(const CPVRChannel &channel, bool bPreview)
{
  // check parental lock state
  if (IsParentalLocked(channel))
    return false;

  // invalid channel
  if (channel.ClientID() < 0)
    return false;

  // check whether we're waiting for a previous switch to complete
  {
    CSingleLock lock(m_critSection);
    if (m_bIsSwitchingChannels)
    {
      CLog::Log(LOGDEBUG, "PVRManager - %s - can't switch to channel '%s'. waiting for the previous switch to complete",
          __FUNCTION__, channel.ChannelName().c_str());
      return false;
    }

    // no need to do anything except switching m_currentFile
    if (bPreview)
    {
      delete m_currentFile;
      m_currentFile = new CFileItem(channel);
      return true;
    }

    m_bIsSwitchingChannels = true;
  }

  CLog::Log(LOGDEBUG, "PVRManager - %s - switching to channel '%s'", __FUNCTION__, channel.ChannelName().c_str());

  // store current time in iLastWatched
  CPVRChannelPtr currentChannel;
  if (m_addons->GetPlayingChannel(currentChannel))
  {
    time_t tNow;
    CDateTime::GetCurrentDateTime().GetAsTime(tNow);
    currentChannel->SetLastWatched(tNow);

    m_channelGroups->SetLastPlayedGroup(GetPlayingGroup(currentChannel->IsRadio()));
  }

  // store channel settings
  SaveCurrentChannelSettings();

  // will be deleted by CPVRChannelSwitchJob::DoWork()
  CFileItem* previousFile = m_currentFile;
  m_currentFile = NULL;

  bool bSwitched(false);

  // switch channel
  if (!m_addons->SwitchChannel(channel))
  {
    // switch failed
    CSingleLock lock(m_critSection);
    m_bIsSwitchingChannels = false;

    CLog::Log(LOGERROR, "PVRManager - %s - failed to switch to channel '%s'", __FUNCTION__, channel.ChannelName().c_str());

    //////////////////BADJIN : 08/03/2016 //////////////
    CStdString msg = StringUtils::Format(g_localizeStrings.Get(19035).c_str(), channel.ChannelName().c_str()); 
    // CHANNELNAME could not be played. Check the log for details.
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error,
        g_localizeStrings.Get(19166), // PVR information
        msg);
    ////////////////////////////////////////////////////
  }
  else
  {
    // switch successful
    bSwitched = true;

    CSingleLock lock(m_critSection);
    m_currentFile = new CFileItem(channel);
    m_bIsSwitchingChannels = false;

    CLog::Log(LOGNOTICE, "PVRManager - %s - switched to channel '%s'", __FUNCTION__, channel.ChannelName().c_str());
  }

  // announce OnStop and OnPlay. yes, this ain't pretty
  {
    CSingleLock lock(m_critSectionTriggers);
    m_pendingUpdates.push_back(new CPVRChannelSwitchJob(previousFile, m_currentFile));
  }
  m_triggerEvent.Set();

  return bSwitched;
}

int CPVRManager::GetTotalTime(void) const
{
  return IsStarted() && m_guiInfo ? m_guiInfo->GetDuration() : 0;
}

int CPVRManager::GetStartTime(void) const
{
  return IsStarted() && m_guiInfo ? m_guiInfo->GetStartTime() : 0;
}

bool CPVRManager::TranslateBoolInfo(DWORD dwInfo) const
{
  return IsStarted() && m_guiInfo ? m_guiInfo->TranslateBoolInfo(dwInfo) : false;
}

bool CPVRManager::TranslateCharInfo(DWORD dwInfo, CStdString &strValue) const
{
  return IsStarted() && m_guiInfo ? m_guiInfo->TranslateCharInfo(dwInfo, strValue) : false;
}

int CPVRManager::TranslateIntInfo(DWORD dwInfo) const
{
  return IsStarted() && m_guiInfo ? m_guiInfo->TranslateIntInfo(dwInfo) : 0;
}

bool CPVRManager::HasTimers(void) const
{
  return IsStarted() && m_timers ? m_timers->HasActiveTimers() : false;
}

bool CPVRManager::IsRecording(void) const
{
  return IsStarted() && m_timers ? m_timers->IsRecording() : false;
}

bool CPVRManager::IsIdle(void) const
{
  if (!IsStarted())
    return true;

  if (IsRecording() || IsPlaying()) // pvr recording or playing?
  {
    return false;
  }
  else if (m_timers) // has active timers, etc.?
  {
    const CDateTime now = CDateTime::GetUTCDateTime();
    const CDateTimeSpan idle(0, 0, CSettings::Get().GetInt("pvrpowermanagement.backendidletime"), 0);

    const CDateTime next = m_timers->GetNextEventTime();
    const CDateTimeSpan delta = next - now;

    if (delta <= idle)
      return false;
  }

  return true;
}

void CPVRManager::ShowPlayerInfo(int iTimeout)
{
  if (IsStarted() && m_guiInfo)
    m_guiInfo->ShowPlayerInfo(iTimeout);
}

void CPVRManager::LocalizationChanged(void)
{
  CSingleLock lock(m_critSection);
  if (IsStarted())
  {
    static_cast<CPVRChannelGroupInternal *>(m_channelGroups->GetGroupAllRadio().get())->CheckGroupName();
    static_cast<CPVRChannelGroupInternal *>(m_channelGroups->GetGroupAllTV().get())->CheckGroupName();
  }
}

bool CPVRManager::EpgsCreated(void) const
{
  CSingleLock lock(m_critSection);
  return m_bEpgsCreated;
}

bool CPVRManager::IsPlayingTV(void) const
{
  return IsStarted() && m_addons && m_addons->IsPlayingTV();
}

bool CPVRManager::IsPlayingRadio(void) const
{
  return IsStarted() && m_addons && m_addons->IsPlayingRadio();
}

bool CPVRManager::IsPlayingRecording(void) const
{
  return IsStarted() && m_addons && m_addons->IsPlayingRecording();
}

bool CPVRManager::IsRunningChannelScan(void) const
{
  return IsStarted() && m_addons && m_addons->IsRunningChannelScan();
}

void CPVRManager::StartChannelScan(void)
{
  if (IsStarted() && m_addons)
    m_addons->StartChannelScan();
}

void CPVRManager::SearchMissingChannelIcons(void)
{
  if (IsStarted() && m_channelGroups)
    m_channelGroups->SearchMissingChannelIcons();
}

bool CPVRManager::IsJobPending(const char *strJobName) const
{
  bool bReturn(false);
  CSingleLock lock(m_critSectionTriggers);
  for (unsigned int iJobPtr = 0; IsStarted() && iJobPtr < m_pendingUpdates.size(); iJobPtr++)
  {
    if (!strcmp(m_pendingUpdates.at(iJobPtr)->GetType(), strJobName))
    {
      bReturn = true;
      break;
    }
  }

  return bReturn;
}

void CPVRManager::QueueJob(CJob *job)
{
  CSingleLock lock(m_critSectionTriggers);
  if (!IsStarted() || IsJobPending(job->GetType()))
  {
    delete job;
    return;
  }

  m_pendingUpdates.push_back(job);

  lock.Leave();
  m_triggerEvent.Set();
}

void CPVRManager::TriggerEpgsCreate(void)
{
  QueueJob(new CPVREpgsCreateJob());
}

void CPVRManager::TriggerRecordingsUpdate(void)
{
  QueueJob(new CPVRRecordingsUpdateJob());
}

void CPVRManager::TriggerTimersUpdate(void)
{
  QueueJob(new CPVRTimersUpdateJob());
}

void CPVRManager::TriggerChannelsUpdate(void)
{
  QueueJob(new CPVRChannelsUpdateJob());
}

void CPVRManager::TriggerChannelGroupsUpdate(void)
{
  QueueJob(new CPVRChannelGroupsUpdateJob());
}

void CPVRManager::TriggerSaveChannelSettings(void)
{
  QueueJob(new CPVRChannelSettingsSaveJob());
}

void CPVRManager::ExecutePendingJobs(void)
{
  CSingleLock lock(m_critSectionTriggers);

  while (m_pendingUpdates.size() > 0)
  {
    CJob *job = m_pendingUpdates.at(0);
    m_pendingUpdates.erase(m_pendingUpdates.begin());
    lock.Leave();

    job->DoWork();
    delete job;

    lock.Enter();
  }

  m_triggerEvent.Reset();
}

bool CPVRManager::OnAction(const CAction &action)
{
  // process PVR specific play actions
  if (action.GetID() == ACTION_PVR_PLAY || action.GetID() == ACTION_PVR_PLAY_TV || action.GetID() == ACTION_PVR_PLAY_RADIO)
  {
    // pvr not active yet, show error message
    if (!IsStarted())
    {
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Warning, g_localizeStrings.Get(19045), g_localizeStrings.Get(19044));
    }
    else
    {
      // see if we're already playing a PVR stream and if not or the stream type
      // doesn't match the demanded type, start playback of according type
      bool isPlayingPvr(IsPlaying() && g_application.CurrentFileItem().HasPVRChannelInfoTag());
      switch (action.GetID())
      {
      case ACTION_PVR_PLAY:
        if (!isPlayingPvr)
          StartPlayback(PlaybackTypeAny);
        break;
      case ACTION_PVR_PLAY_TV:
        if (!isPlayingPvr || g_application.CurrentFileItem().GetPVRChannelInfoTag()->IsRadio())
          StartPlayback(PlaybackTypeTv);
        break;
      case ACTION_PVR_PLAY_RADIO:
        if (!isPlayingPvr || !g_application.CurrentFileItem().GetPVRChannelInfoTag()->IsRadio())
          StartPlayback(PlaybackTypeRadio);
        break;
      }
    }
    return true;
  }
  return false;
}

void CPVRManager::SettingOptionsPvrStartLastChannelFiller(const CSetting *setting, std::vector< std::pair<std::string, int> > &list, int &current)
{
  list.push_back(make_pair(g_localizeStrings.Get(106),   PVR::START_LAST_CHANNEL_OFF));
  //(-/+) BASH - ECO - START
  //list.push_back(make_pair(g_localizeStrings.Get(19190), PVR::START_LAST_CHANNEL_MIN));
  //(-/+) BASH - ECO - FINISH
  list.push_back(make_pair(g_localizeStrings.Get(107),   PVR::START_LAST_CHANNEL_ON));
}

bool CPVRChannelSwitchJob::DoWork(void)
{
  // announce OnStop and delete m_previous when done
  if (m_previous)
  {
    CVariant data(CVariant::VariantTypeObject);
    data["end"] = true;
    ANNOUNCEMENT::CAnnouncementManager::Announce(ANNOUNCEMENT::Player, "xbmc", "OnStop", CFileItemPtr(m_previous), data);
  }

  // announce OnPlay if the switch was successful
  if (m_next)
  {
    CVariant param;
    param["player"]["speed"] = 1;
    param["player"]["playerid"] = g_playlistPlayer.GetCurrentPlaylist();
    ANNOUNCEMENT::CAnnouncementManager::Announce(ANNOUNCEMENT::Player, "xbmc", "OnPlay", CFileItemPtr(new CFileItem(*m_next)), param);
  }

  return true;
}

bool CPVRManager::CreateChannelEpgs(void)
{
  if (EpgsCreated())
    return true;

  CSingleLock lock(m_critSection);
  m_bEpgsCreated = m_channelGroups->CreateChannelEpgs();
  return m_bEpgsCreated;
}

map<int, int>CPVRManager::m_recordingMap;




