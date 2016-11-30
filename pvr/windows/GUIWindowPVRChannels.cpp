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

#include "GUIWindowPVRChannels.h"

#include "dialogs/GUIDialogFileBrowser.h"
#include "dialogs/GUIDialogNumeric.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogOK.h"
#include "dialogs/GUIDialogYesNo.h"
#include "guilib/GUIKeyboardFactory.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/Key.h"
#include "GUIInfoManager.h"
#include "profiles/ProfilesManager.h"
#include "pvr/PVRManager.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "pvr/dialogs/GUIDialogPVRGroupManager.h"
#include "pvr/windows/GUIWindowPVR.h"
#include "pvr/addons/PVRClients.h"
#include "pvr/timers/PVRTimers.h"
#include "epg/EpgContainer.h"
#include "settings/Settings.h"
#include "storage/MediaManager.h"
#include "utils/log.h"
#include "threads/SingleLock.h"
#include "guilib/GUIWindowManager.h"
#include "windows/GUIWindowHome.h"
#include "settings/windows/GUIWindowSettings.h"
#include "settings//windows/GUIWindowSettingsCategory.h"
//TBIRD 1.3.6 MODS - START
#include "interfaces/Builtins.h"
#include "pvr/dialogs/GUIDialogPVRGuideInfo.h"
#include "pvr/timers/PVRTimerInfoTag.h"
//TBIRD 1.3.6 MODS - FINISH
//HARSH-TBIRD - ECO - START
#include <string>
#include <string.h>
#include "utils/XBMCTinyXML.h"
#include "utils/XMLUtils.h"
#include <fstream>

#include "utils/StringUtils.h"
#define IPTV_List_ELEMENT 30001
#include "guilib/LocalizeStrings.h"
#include "Application.h"
#include "dialogs/GUIDialogYesNo.h"
#include "XBDateTime.h"

#include "settings/lib/Setting.h"
int numIPTVListLang;
 

using namespace PVR;
using namespace EPG;

CGUIWindowPVRChannels::CGUIWindowPVRChannels(CGUIWindowPVR *parent, bool bRadio) :
  CGUIWindowPVRCommon(parent,
                      bRadio ? PVR_WINDOW_CHANNELS_RADIO : PVR_WINDOW_CHANNELS_TV,
                      bRadio ? CONTROL_BTNCHANNELS_RADIO : CONTROL_BTNCHANNELS_TV,
                      bRadio ? CONTROL_LIST_CHANNELS_RADIO: CONTROL_LIST_CHANNELS_TV)
{
  m_bRadio              = bRadio;
  m_bShowHiddenChannels = false;
}

CGUIWindowPVRChannels::~CGUIWindowPVRChannels(void)
{
}

void CGUIWindowPVRChannels::ResetObservers(void)
{
  CSingleLock lock(m_critSection);
  g_EpgContainer.RegisterObserver(this);
  g_PVRTimers->RegisterObserver(this);
  g_infoManager.RegisterObserver(this);
}

void CGUIWindowPVRChannels::UnregisterObservers(void)
{
  CSingleLock lock(m_critSection);
  g_EpgContainer.UnregisterObserver(this);
  if (g_PVRTimers)
    g_PVRTimers->UnregisterObserver(this);
  g_infoManager.UnregisterObserver(this);
}

void CGUIWindowPVRChannels::GetContextButtons(int itemNumber, CContextButtons &buttons) const
{
  //(-/+) TBIRD 1.3.6 MODS - START  
  if (itemNumber < 0 || itemNumber >= m_parent->m_vecItems->Size())
    itemNumber = 0;
  //(-/+) TBIRD 13.6 MODS - FINISH

  CFileItemPtr pItem = m_parent->m_vecItems->Get(itemNumber);
  CPVRChannel *channel = pItem->GetPVRChannelInfoTag();

  if (pItem->GetPath() == "pvr://channels/.add.channel")
  {
    /* If yes show only "New Channel" on context menu */
    buttons.Add(CONTEXT_BUTTON_ADD, 19046);                                           /* add new channel */
  }
  //TBIRD-STAN 1.3.5 MODS - START
  //TBIRD 1.3.4 MODS - START
  /*Commenting  this code as per the new requirement - TBIRD
  //Removed the buttons in the context menu added the functionality to pop-up the 'tv' window; Need to call a python script for that
  CBuiltins *pBuiltin = new CBuiltins(); 
  pBuiltin->Execute("XBMC.RunScript(/storage/.xbmc/addons/script.showlivetvsettings/default.py)");
  */
  /*buttons.Add(CONTEXT_BUTTON_USER1, 36044);
  buttons.Add(CONTEXT_BUTTON_USER2, 36045);
  buttons.Add(CONTEXT_BUTTON_USER3, 36046);
  buttons.Add(CONTEXT_BUTTON_USER4, 36047);
  buttons.Add(CONTEXT_BUTTON_USER5, 36048); // Search Option*/
  //TBIRD 1.3.4 MODS - FINISH
  //TBIRD-STAN 1.3.5 MODS - FINISH
}

bool CGUIWindowPVRChannels::OnContextButton(int itemNumber, CONTEXT_BUTTON button)
{
  if (itemNumber < 0 || itemNumber >= (int) m_parent->m_vecItems->Size())
    return CGUIWindowPVRCommon::OnContextButton(itemNumber, button);
  
  CFileItemPtr pItem = m_parent->m_vecItems->Get(itemNumber);

  return OnContextButtonPlay(pItem.get(), button) ||
      OnContextButtonMove(pItem.get(), button) ||
      OnContextButtonHide(pItem.get(), button) ||
      OnContextButtonShowHidden(pItem.get(), button) ||
      OnContextButtonSetThumb(pItem.get(), button) ||
      OnContextButtonAdd(pItem.get(), button) ||
      OnContextButtonInfo(pItem.get(), button) ||
      OnContextButtonGroupManager(pItem.get(), button) ||
      OnContextButtonFilter(pItem.get(), button) ||
      OnContextButtonUpdateEpg(pItem.get(), button) ||
      //TBIRD-1.3.6 MODS - START
      OnContextButtonShowEpg(pItem.get(), button) ||
      //TBIRD-1.3.6 MODS - FINISH
      OnContextButtonRecord(pItem.get(), button) ||
      OnContextButtonLock(pItem.get(), button) ||
      //TBIRD-STAN 1.3.5 MODS - START
      //TBIRD 1.3.4 MODS - START
      /*OnContextButtonUser1(pItem.get(), button) ||
      OnContextButtonUser2(pItem.get(), button) ||
      OnContextButtonUser3(pItem.get(), button) ||
      OnContextButtonUser4(pItem.get(), button) ||
      OnContextButtonUser5(pItem.get(), button) ||*/
      //TBIRD 1.3.4 MODS - FINISH
      //TBIRD-STAN 1.3.5 MODS - FINISH
      CGUIWindowPVRCommon::OnContextButton(itemNumber, button);
}

CPVRChannelGroupPtr CGUIWindowPVRChannels::SelectedGroup(void)
{
  if (!m_selectedGroup)
    SetSelectedGroup(g_PVRManager.GetPlayingGroup(m_bRadio));

  return m_selectedGroup;
}

void CGUIWindowPVRChannels::SetSelectedGroup(CPVRChannelGroupPtr group)
{
  if (!group)
    return;

  if (m_selectedGroup)
    m_selectedGroup->UnregisterObserver(this);
  m_selectedGroup = group;
  m_selectedGroup->RegisterObserver(this);
  g_PVRManager.SetPlayingGroup(m_selectedGroup);
}

void CGUIWindowPVRChannels::Notify(const Observable &obs, const ObservableMessage msg)
{
  if (msg == ObservableMessageChannelGroup || msg == ObservableMessageTimers || msg == ObservableMessageEpgActiveItem || msg == ObservableMessageCurrentItem)
  {
    if (IsVisible())
      SetInvalid();
    else
      m_bUpdateRequired = true;
  }
  else if (msg == ObservableMessageChannelGroupReset)
  {
    if (IsVisible())
      UpdateData(true);
    else
      m_bUpdateRequired = true;
  }
}

CPVRChannelGroupPtr CGUIWindowPVRChannels::SelectNextGroup(void)
{
  CPVRChannelGroupPtr currentGroup = SelectedGroup();
  CPVRChannelGroupPtr nextGroup = currentGroup->GetNextGroup();
  while (nextGroup && nextGroup->Size() == 0 &&
      // break if the group matches
      *nextGroup != *currentGroup &&
      // or if we hit the first group
      !nextGroup->IsInternalGroup())
    nextGroup = nextGroup->GetNextGroup();

  /* always update so users can reset the list */
  if (nextGroup)
  {
    SetSelectedGroup(nextGroup);
    UpdateData();
  }

  return m_selectedGroup;
}

void CGUIWindowPVRChannels::UpdateData(bool bUpdateSelectedFile /* = true */)
{
  CSingleLock lock(m_critSection);
  CLog::Log(LOGDEBUG, "CGUIWindowPVRChannels - %s - update window '%s'. set view to %d",
      __FUNCTION__, GetName(), m_iControlList);
  m_bUpdateRequired = false;

  /* lock the graphics context while updating */
  CSingleLock graphicsLock(g_graphicsContext);

  CPVRChannelGroupPtr selectedGroup = SelectedGroup();

  if (!bUpdateSelectedFile)
    m_iSelected = m_parent->m_viewControl.GetSelectedItem();
  else
    m_parent->m_viewControl.SetSelectedItem(0);

  m_parent->m_viewControl.SetCurrentView(m_iControlList);
  ShowBusyItem();
  m_parent->m_vecItems->Clear();

  CPVRChannelGroupPtr currentGroup = g_PVRManager.GetPlayingGroup(m_bRadio);
  if (!currentGroup)
    return;

  SetSelectedGroup(currentGroup);

  CStdString strPath;
  strPath = StringUtils::Format("pvr://channels/%s/%s/",
      m_bRadio ? "radio" : "tv",
      m_bShowHiddenChannels ? ".hidden" : currentGroup->GroupName().c_str());

  m_parent->m_vecItems->SetPath(strPath);
  m_parent->Update(m_parent->m_vecItems->GetPath());
  m_parent->m_viewControl.SetItems(*m_parent->m_vecItems);

  if (bUpdateSelectedFile)
  {
    if (!SelectPlayingFile())
      m_parent->m_viewControl.SetSelectedItem(m_iSelected);
  }

  /* empty list */
  if (m_parent->m_vecItems->Size() == 0)
  {
    if (m_bShowHiddenChannels)
    {
      /* show the visible channels instead */
      m_bShowHiddenChannels = false;
      graphicsLock.Leave();
      lock.Leave();

      UpdateData(bUpdateSelectedFile);
      return;
    }
    else if (currentGroup->GroupID() > 0)
    {
      if (*currentGroup != *SelectNextGroup())
        return;
    }
  }

  m_parent->SetLabel(CONTROL_LABELHEADER, g_localizeStrings.Get(m_bRadio ? 19024 : 19023));
  if (m_bShowHiddenChannels)
    m_parent->SetLabel(CONTROL_LABELGROUP, g_localizeStrings.Get(19022));
  else
    m_parent->SetLabel(CONTROL_LABELGROUP, currentGroup->GroupName());
}

bool CGUIWindowPVRChannels::OnClickButton(CGUIMessage &message)
{
  bool bReturn = false;

  if (IsSelectedButton(message))
  {
    bReturn = true;
    SelectNextGroup();
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnClickList(CGUIMessage &message)
{
  bool bReturn = false;

  if (IsSelectedList(message))
  {
    bReturn = true;
    int iAction = message.GetParam1();
    int iItem = m_parent->m_viewControl.GetSelectedItem();

    if (iItem < 0 || iItem >= (int) m_parent->m_vecItems->Size())
      return bReturn;
    CFileItemPtr pItem = m_parent->m_vecItems->Get(iItem);

    /* process actions */
    if (iAction == ACTION_SELECT_ITEM || iAction == ACTION_MOUSE_LEFT_CLICK || iAction == ACTION_PLAY)
    //(-/+)TBIRD 1.3.6 MODS - START
    {      
      if (!g_PVRManager.IsParentalLocked(pItem.get()))
      {      
         ActionPlayChannel(pItem.get());
         return true;
      }
      else
      {
         if (g_PVRManager.CheckParentalPIN(g_localizeStrings.Get(19262).c_str()))
         {
            ActionPlayChannel(pItem.get());
            return true;
         }
      }
    }
    //(-/+)TBIRD 1.3.6 MODS - FINISH
    else if (iAction == ACTION_SHOW_INFO)
      ShowEPGInfo(pItem.get());
    else if (iAction == ACTION_DELETE_ITEM)
      ActionDeleteChannel(pItem.get());
    else if (iAction == ACTION_CONTEXT_MENU || iAction == ACTION_MOUSE_RIGHT_CLICK)
      m_parent->OnPopupMenu(iItem);
    else
      bReturn = false;
  }
  
  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonAdd(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_ADD)
  {
    CGUIDialogOK::ShowAndGetInput(19033,0,19038,0);
    bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonGroupManager(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_GROUP_MANAGER)
  {
    ShowGroupManager();
    bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonHide(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_HIDE)
  {
    CPVRChannel *channel = item->GetPVRChannelInfoTag();
    if (!channel || channel->IsRadio() != m_bRadio)
      return bReturn;

    CGUIDialogYesNo* pDialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
    if (!pDialog)
      return bReturn;

    pDialog->SetHeading(19039);
    pDialog->SetLine(0, "");
    pDialog->SetLine(1, channel->ChannelName());
    pDialog->SetLine(2, "");
    pDialog->DoModal();

    if (!pDialog->IsConfirmed())
      return bReturn;

    g_PVRManager.GetPlayingGroup(m_bRadio)->RemoveFromGroup(*channel);
    UpdateData();

    bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonLock(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_ADD_LOCK)
  {
    // ask for PIN first
    if (!g_PVRManager.CheckParentalPIN(g_localizeStrings.Get(19262).c_str()))
      return bReturn;

    CPVRChannelGroupPtr group = g_PVRChannelGroups->GetGroupAll(m_bRadio);
    if (!group)
      return bReturn;

    group->ToggleChannelLocked(*item);
    UpdateData();

    bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonInfo(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_INFO)
  {
    ShowEPGInfo(item);
    bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonMove(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_MOVE)
  {
    CPVRChannel *channel = item->GetPVRChannelInfoTag();
    if (!channel || channel->IsRadio() != m_bRadio)
      return bReturn;

    CStdString strIndex;
    strIndex = StringUtils::Format("%i", channel->ChannelNumber());
    CGUIDialogNumeric::ShowAndGetNumber(strIndex, g_localizeStrings.Get(19052));
    int newIndex = atoi(strIndex.c_str());

    if (newIndex != channel->ChannelNumber())
    {
      g_PVRManager.GetPlayingGroup()->MoveChannel(channel->ChannelNumber(), newIndex);
      UpdateData();
    }

    bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonPlay(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_PLAY_ITEM)
  {
    /* play channel */
    bReturn = PlayFile(item, CSettings::Get().GetBool("pvrplayback.playminimized"));
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonSetThumb(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_SET_THUMB)
  {
    if (CProfilesManager::Get().GetCurrentProfile().canWriteSources() && !g_passwordManager.IsProfileLockUnlocked())
      return bReturn;
    else if (!g_passwordManager.IsMasterLockUnlocked(true))
      return bReturn;

    /* setup our thumb list */
    CFileItemList items;
    CPVRChannel *channel = item->GetPVRChannelInfoTag();

    if (!channel->IconPath().empty())
    {
      /* add the current icon, if available */
      CFileItemPtr current(new CFileItem("thumb://Current", false));
      current->SetArt("thumb", channel->IconPath());
      current->SetLabel(g_localizeStrings.Get(19282));
      items.Add(current);
    }
    else if (item->HasArt("thumb"))
    {
      /* already have a thumb that the share doesn't know about - must be a local one, so we may as well reuse it */
      CFileItemPtr current(new CFileItem("thumb://Current", false));
      current->SetArt("thumb", item->GetArt("thumb"));
      current->SetLabel(g_localizeStrings.Get(19282));
      items.Add(current);
    }

    /* and add a "no thumb" entry as well */
    CFileItemPtr nothumb(new CFileItem("thumb://None", false));
    nothumb->SetIconImage(item->GetIconImage());
    nothumb->SetLabel(g_localizeStrings.Get(19283));
    items.Add(nothumb);

    CStdString strThumb;
    VECSOURCES shares;
    if (CSettings::Get().GetString("pvrmenu.iconpath") != "")
    {
      CMediaSource share1;
      share1.strPath = CSettings::Get().GetString("pvrmenu.iconpath");
      share1.strName = g_localizeStrings.Get(19066);
      shares.push_back(share1);
    }
    g_mediaManager.GetLocalDrives(shares);
    if (!CGUIDialogFileBrowser::ShowAndGetImage(items, shares, g_localizeStrings.Get(19285), strThumb, NULL, 19285))
      return bReturn;

    if (strThumb != "thumb://Current")
    {
      if (strThumb == "thumb://None")
        strThumb = "";

      CPVRChannelGroupPtr group = g_PVRChannelGroups->GetGroupAll(channel->IsRadio());
      CPVRChannelPtr channelPtr = group->GetByUniqueID(channel->UniqueID());

      channelPtr->SetIconPath(strThumb, true);
      channelPtr->Persist();
      UpdateData();
    }

    bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonShowHidden(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_SHOW_HIDDEN)
  {
    m_bShowHiddenChannels = !m_bShowHiddenChannels;
    UpdateData();
    bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonFilter(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_FILTER)
  {
    CStdString filter = m_parent->GetProperty("filter").asString();
    CGUIKeyboardFactory::ShowAndGetFilter(filter, false);
    m_parent->OnFilterItems(filter);

    bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonRecord(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn(false);
  
  if (button == CONTEXT_BUTTON_RECORD_ITEM)
  {    
    //(-/+) TBIRD MODS 1.3.6 - START
    int iItem = m_parent->m_viewControl.GetSelectedItem();

    if (iItem < 0)
       return false;

    CStdString clientName;
    CPVRChannel *channel = item->GetPVRChannelInfoTag();
    g_PVRClients->GetClientName(channel->ClientID(), clientName);
    
    if (clientName != "IPTV Simple PVR Add-on:connected")
    {
       if (channel)
          return g_PVRManager.ToggleRecordingOnChannel(channel->ChannelID());
    } 
    else
    {
       CGUIDialogOK::ShowAndGetInput(19033,0,37006,0);
       return true;
    }
    //(-/+) TBIRD MODS 1.3.6 - FINISH
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonUpdateEpg(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_UPDATE_EPG)
  {
    CGUIDialogYesNo* pDialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
    if (!pDialog)
      return bReturn;

    CPVRChannel *channel = item->GetPVRChannelInfoTag();
    pDialog->SetHeading(19251);
    pDialog->SetLine(0, g_localizeStrings.Get(19252));
    pDialog->SetLine(1, channel->ChannelName());
    pDialog->SetLine(2, "");
    pDialog->DoModal();

    if (!pDialog->IsConfirmed())
      return bReturn;

    bReturn = UpdateEpgForChannel(item);

    CStdString strMessage = StringUtils::Format("%s: '%s'", g_localizeStrings.Get(bReturn ? 19253 : 19254).c_str(), channel->ChannelName().c_str());
    CGUIDialogKaiToast::QueueNotification(bReturn ? CGUIDialogKaiToast::Info : CGUIDialogKaiToast::Error,
        g_localizeStrings.Get(19166),
        strMessage);
  }

  return bReturn;
}
//TBIRD 1.3.6 MODS - START
bool CGUIWindowPVRChannels::OnContextButtonShowEpg(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_SHOW_EPG)
  {
     int iItem = m_parent->m_viewControl.GetSelectedItem();

     if (iItem < 0)
        return false;
     
     CBuiltins *pBuiltin = new CBuiltins();
     pBuiltin->Execute("XBMC.RunScript(/storage/.xbmc/addons/script.showlivetvsettings/epg.py)");
     return true;    
  }

  return bReturn;
}
//TBIRD 1.3.6 MODS - FINISH

//TBIRD-STAN 1.3.5 MODS - START
//TBIRD 1.3.4 MODS - START
/*bool CGUIWindowPVRChannels::OnContextButtonUser1(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_USER1)
  {
      CBuiltins *pBuiltin = new CBuiltins();
      pBuiltin->Execute("XBMC.RunScript(/storage/.xbmc/addons/script.showlivetvsettings/livetv.py)");
      bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonUser2(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_USER2)
  {
	  CBuiltins *pBuiltin = new CBuiltins();
	  pBuiltin->Execute("XBMC.RunScript(/storage/.xbmc/addons/script.showlivetvsettings/epg.py)");
	  bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonUser3(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_USER3)
  {
	  CBuiltins *pBuiltin = new CBuiltins();
	  pBuiltin->Execute("XBMC.RunScript(/storage/.xbmc/addons/script.showlivetvsettings/recordings.py)");
	  bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonUser4(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_USER4)
  {
	  CBuiltins *pBuiltin = new CBuiltins();
	  pBuiltin->Execute("XBMC.RunScript(/storage/.xbmc/addons/script.showlivetvsettings/timers.py)");
	  bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRChannels::OnContextButtonUser5(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_USER5)
  {
	  CGUIMessage msg(GUI_MSG_CLICKED, 36, 10601, 0);
	  g_windowManager.SendMessage(msg); 
	  bReturn = true;
  }

  return bReturn;
}*/
//TBIRD 1.3.4 MODS - FINISH
//TBIRD-STAN 1.3.5 MODS - FINISH

void CGUIWindowPVRChannels::ShowGroupManager(void)
{
  /* Load group manager dialog */
  CGUIDialogPVRGroupManager* pDlgInfo = (CGUIDialogPVRGroupManager*)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_GROUP_MANAGER);
  if (!pDlgInfo)
    return;

  pDlgInfo->SetRadio(m_bRadio);
  pDlgInfo->DoModal();

  return;
}

//HARSH-TBIRD - ECO - START
void CGUIWindowPVRChannels::SettingOptionsIPTVList(const CSetting *setting, std::vector< std::pair<std::string, std::string> > &list, std::string &current)
{
  CXBMCTinyXML doc;
  FILE *f = fopen("/storage/.xbmc/system/iptv/IPTV_List.xml", "r");
  doc.LoadFile(f);
  fclose(f);
  const TiXmlElement * languages = doc.RootElement();
  unsigned int currentNode = 0;
  while(languages)
  {
    const TiXmlElement *language = languages->FirstChildElement("language");
    while(language)
    {
      std::string id = language->Attribute("name");
      list.push_back(make_pair(id, id));
      currentNode++;
      language = language->NextSiblingElement("language");
    }
    numIPTVListLang = currentNode;
    languages = languages->NextSiblingElement("languages");
  }
} 
 
int CGUIWindowPVRChannels::getLanNode(std::string strLan)
{
  int iLanNode = -1;
  CXBMCTinyXML doc;
  FILE *f = fopen("/storage/.xbmc/system/iptv/IPTV_List.xml", "r");
  doc.LoadFile(f);
  fclose(f);
  TiXmlElement * languages = doc.RootElement();
  std::string currLan;
  TiXmlElement *language = languages->FirstChildElement("language");
  while(language)
  {    
    iLanNode++;
    currLan = language->Attribute("name"); 
    if(strLan.compare(currLan) == 0) break;
    language = language->NextSiblingElement("language");
  }
  return iLanNode;
}
 
std::string CGUIWindowPVRChannels::getEPG(unsigned int node)
{
  CXBMCTinyXML doc;
  FILE *f = fopen("/storage/.xbmc/system/iptv/IPTV_List.xml", "r");
  doc.LoadFile(f);
  fclose(f);
  TiXmlElement * languages = doc.RootElement();
  unsigned int currentNode = 0;
  std::string epgUrl;
  while(languages)
  {
    TiXmlElement *language = languages->FirstChildElement("language");
    while(language)
    {
      std::string id = language->Attribute("name");
      if(currentNode == node)
      {
        TiXmlElement *url = language->FirstChildElement("epgUrl");
        if(url)
        {
          epgUrl = url->FirstChild()->ValueStr(); 
        } 
      }
      currentNode++;
      language = language->NextSiblingElement("language");
    }
    languages = languages->NextSiblingElement("languages");
  }
  return epgUrl; 
}

std::string CGUIWindowPVRChannels::getM3U(unsigned int node)
{
  CXBMCTinyXML doc;
  //std:string m3uUrl;
  //(-/+) TBIRD - ECO - START  
  //Need to check whether the file exists:
  //ifstream ifile("/storage/.xbmc/system/iptv/IPTV_List.xml");
  //if (ifile) 
  //{     
  FILE *f = fopen("/storage/.xbmc/system/iptv/IPTV_List.xml", "r");
  doc.LoadFile(f);
  fclose(f);
  TiXmlElement * languages = doc.RootElement();
  unsigned int currentNode = 0;
  std::string m3uUrl;
  while(languages)
  {
    TiXmlElement *language = languages->FirstChildElement("language");
    while(language)
    {
      std::string id = language->Attribute("name");
      if(currentNode == node)
      {
        TiXmlElement *url = language->FirstChildElement("m3uUrl");
        if(url)
        {
          m3uUrl = url->FirstChild()->ValueStr(); 
        } 
      }
      currentNode++;
      language = language->NextSiblingElement("language");
    }
    languages = languages->NextSiblingElement("languages");
  }
  return m3uUrl; 
  //}
  //(-/+) TBIRD - ECO - FINISH
}

void CGUIWindowPVRChannels::updateIPTVSetting(std::string epgUrl, std::string m3uUrl)
{
  CLog::Log(LOGNOTICE,"HPrasad - %s - CGUIWindowPVRChannels", __FUNCTION__); 
  CXBMCTinyXML doc, doc2;
  CStdString        m_userSettingsPath;
  std::map<CStdString, CStdString> m_settings;
  FILE *f = fopen("/storage/.xbmc/userdata/addon_data/pvr.iptvsimple/settings.xml", "r");
  doc.LoadFile(f);
  fclose(f);
  const TiXmlElement * settings = doc.RootElement();

  if (!settings)
    settings = doc.RootElement();

  bool foundSetting = false;
  const TiXmlElement *setting = settings->FirstChildElement("setting");
  while (setting)
  {
    const char *id = setting->Attribute("id");
    const char *value = setting->Attribute("value");
    CLog::Log(LOGNOTICE,"HPrasad - %s [%s - %s]- CGUIWindowPVRChannels", __FUNCTION__, id, value);
    if (id && value)
    {
      if( strcmp(id, "epgUrl") == 0 )
      {
        m_settings[id] = epgUrl;
      }
      else if ( strcmp(id, "m3uUrl") == 0 )
      {
        m_settings[id] = m3uUrl;
      }
      else
      {
        m_settings[id] = value;
      }
      foundSetting = true;
    }
    setting = setting->NextSiblingElement("setting");
  }
   
  TiXmlElement node("settings");
  doc2.InsertEndChild(node);
  for (std::map<CStdString, CStdString>::const_iterator i = m_settings.begin(); i != m_settings.end(); ++i)
  {
    TiXmlElement nodeSetting("setting");
    nodeSetting.SetAttribute("id", i->first.c_str());
    nodeSetting.SetAttribute("value", i->second.c_str());
    doc2.RootElement()->InsertEndChild(nodeSetting);
  }
  doc2.SaveFile("/storage/.xbmc/userdata/addon_data/pvr.iptvsimple/settings.xml");
}
