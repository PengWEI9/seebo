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

#include "GUIWindowPVRCommon.h"

#include "Application.h"
#include "ApplicationMessenger.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "dialogs/GUIDialogOK.h"
#include "dialogs/GUIDialogYesNo.h"
#include "filesystem/StackDirectory.h"
#include "guilib/GUIMessage.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/Key.h"
#include "guilib/LocalizeStrings.h"
#include "pvr/PVRManager.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "pvr/dialogs/GUIDialogPVRGuideInfo.h"
#include "pvr/dialogs/GUIDialogPVRRecordingInfo.h"
#include "pvr/dialogs/GUIDialogPVRTimerSettings.h"
#include "epg/Epg.h"
#include "pvr/timers/PVRTimers.h"
#include "pvr/addons/PVRClients.h"
#include "pvr/windows/GUIWindowPVR.h"
#include "pvr/windows/GUIWindowPVRSearch.h"
#include "pvr/recordings/PVRRecordings.h"
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "utils/log.h"
#include "utils/URIUtils.h"
#include "GUIUserMessages.h"
#include "cores/IPlayer.h"
//TBIRD 1.3.6 MODS - START
#include "epg/EpgContainer.h"
//TBIRD 1.3.6 MODS - FINISH

using namespace std;
using namespace PVR;
using namespace EPG;

CGUIWindowPVRCommon::CGUIWindowPVRCommon(CGUIWindowPVR *parent, PVRWindow window,
    unsigned int iControlButton, unsigned int iControlList)
{
  m_parent          = parent;
  m_window          = window;
  m_iControlButton  = iControlButton;
  m_iControlList    = iControlList;
  m_bUpdateRequired = false;
  m_iSelected       = 0;
  m_iSortOrder      = SortOrderAscending;
  m_iSortMethod     = SortByDate;
  m_iSortAttributes = SortAttributeNone;
  if( m_parent->GetViewState() )
  {
    SortDescription sorting = m_parent->GetViewState()->GetSortMethod();
    m_iSortOrder      = sorting.sortOrder;
    m_iSortMethod     = sorting.sortBy;
    m_iSortAttributes = sorting.sortAttributes;
  }
}

bool CGUIWindowPVRCommon::operator ==(const CGUIWindowPVRCommon &right) const
{
  return (this == &right || m_window == right.m_window);
}

bool CGUIWindowPVRCommon::operator !=(const CGUIWindowPVRCommon &right) const
{
  return !(*this == right);
}

const char *CGUIWindowPVRCommon::GetName(void) const
{
  switch(m_window)
  {
  case PVR_WINDOW_EPG:
    return "epg";
  case PVR_WINDOW_CHANNELS_RADIO:
    return "radio";
  case PVR_WINDOW_CHANNELS_TV:
    return "tv";
  case PVR_WINDOW_RECORDINGS:
    return "recordings";
  case PVR_WINDOW_SEARCH:
    return "search";
  case PVR_WINDOW_TIMERS:
    return "timers";
  default:
    return "unknown";
  }
}

bool CGUIWindowPVRCommon::IsFocused(void) const
{
  return !g_application.IsPlayingFullScreenVideo() &&
      g_windowManager.GetFocusedWindow() == WINDOW_PVR &&
      IsActive();
}

bool CGUIWindowPVRCommon::IsVisible(void) const
{
  return !g_application.IsPlayingFullScreenVideo() &&
      g_windowManager.GetActiveWindow() == WINDOW_PVR &&
      IsActive();
}

bool CGUIWindowPVRCommon::IsActive(void) const
{
  CGUIWindowPVRCommon *window = m_parent->GetActiveView();
  return (window && *window == *this);
}

bool CGUIWindowPVRCommon::IsSavedView(void) const
{
  CGUIWindowPVRCommon *window = m_parent->GetSavedView();
  return (window && *window == *this);
}

bool CGUIWindowPVRCommon::IsSelectedButton(CGUIMessage &message) const
{
  return (message.GetSenderId() == (int) m_iControlButton);
}

bool CGUIWindowPVRCommon::IsSelectedControl(CGUIMessage &message) const
{
  return (message.GetControlId() == (int) m_iControlButton);
}

bool CGUIWindowPVRCommon::IsSelectedList(CGUIMessage &message) const
{
  return (message.GetSenderId() == (int) m_iControlList);
}

void CGUIWindowPVRCommon::SetInvalid()
{
  for (int iItemPtr = 0; iItemPtr < m_parent->m_vecItems->Size(); iItemPtr++)
    m_parent->m_vecItems->Get(iItemPtr)->SetInvalid();
  m_parent->SetInvalid();
}

void CGUIWindowPVRCommon::OnInitWindow()
{
  m_parent->m_viewControl.SetCurrentView(m_iControlList);
}

bool CGUIWindowPVRCommon::SelectPlayingFile(void)
{
  bool bReturn(false);

  if (g_PVRManager.IsPlaying())
  {
    m_parent->m_viewControl.SetSelectedItem(g_application.CurrentFile());
    bReturn = true;
  }

  return bReturn;
}

bool CGUIWindowPVRCommon::OnMessageFocus(CGUIMessage &message)
{
  bool bReturn = false;

  if (message.GetMessage() == GUI_MSG_FOCUSED &&
      (IsSelectedControl(message) || IsSavedView()))
  {
    CLog::Log(LOGDEBUG, "CGUIWindowPVRCommon - %s - focus set to window '%s'", __FUNCTION__, GetName());
    bool bIsActive = IsActive();
    m_parent->SetActiveView(this);

    if (!bIsActive || m_bUpdateRequired)
      UpdateData();

    bReturn = true;
  }

  return bReturn;
}

void CGUIWindowPVRCommon::OnWindowUnload(void)
{
  m_iSelected = m_parent->m_viewControl.GetSelectedItem();
  m_history = m_parent->m_history;
}

bool CGUIWindowPVRCommon::OnAction(const CAction &action)
{
  bool bReturn = false;

  if (action.GetID() == ACTION_NAV_BACK ||
      action.GetID() == ACTION_PREVIOUS_MENU)
  {
    g_windowManager.PreviousWindow();
    bReturn = true;
  }

  return bReturn;
}
//TBIRD - STAN 1.3.5 MODS - START
/*
bool CGUIWindowPVRCommon::OnContextButton(int itemNumber, CONTEXT_BUTTON button)
{
  if (itemNumber < 0 || itemNumber >= (int) m_parent->m_vecItems->Size())
    return false;
  CFileItemPtr pItem = m_parent->m_vecItems->Get(itemNumber);

  return (OnContextButtonSortAsc(pItem.get(), button) ||
      OnContextButtonSortBy(pItem.get(), button) ||
      OnContextButtonSortByChannel(pItem.get(), button) ||
      OnContextButtonSortByName(pItem.get(), button) ||
      OnContextButtonSortByDate(pItem.get(), button) ||
      OnContextButtonFind(pItem.get(), button) ||
      OnContextButtonMenuHooks(pItem.get(), button));
}
*/

void CGUIWindowPVRCommon::GetContextButtons(int itemNumber, CContextButtons &buttons)
{
  if (itemNumber < 0 || itemNumber >= (int) m_parent->m_vecItems->Size())
    itemNumber = 0;

  buttons.Add(CONTEXT_BUTTON_PVR_CHANNELS, 37044);
  buttons.Add(CONTEXT_BUTTON_PVR_GUIDE, 37045);
  buttons.Add(CONTEXT_BUTTON_PVR_RECORDING, 37046);
  buttons.Add(CONTEXT_BUTTON_PVR_TIMERS, 37047);
  buttons.Add(CONTEXT_BUTTON_PVR_SEARCH, 37048);

  const char *windowName = GetName();

  if (windowName=="search")
  {     
     buttons.Add(CONTEXT_BUTTON_USER1, 37067);
     buttons.Add(CONTEXT_BUTTON_USER2, 37068); //Tuan's mod
  }
 
  //TBIRD 1.3.5 MODS - START
  if (windowName=="recordings")
  {     
    //TUAN's MODS 1.3.6 - START	
	CFileItemPtr pItem0 =  m_parent->m_vecItems->Get(0);
	int totalItem       =  m_parent->m_vecItems->Size();
    /* Only add DELETE buttons if the item list is not empty */ 
	if (pItem0.get()->GetPath() != "pvr://recordings/" && pItem0.get()->GetPath() != "" && totalItem > 0)
	{
	     buttons.Add(CONTEXT_BUTTON_DELETE, 117);  
	     buttons.Add(CONTEXT_BUTTON_DELETE_ALL_REC, 37101);//TUAN'S MODS 1.3.6 
		 CLog::Log(LOGDEBUG, "CGUIWindowPVRCommon - %s: Add DELETE buttons",__FUNCTION__);
	}
    //TUAN's MODS 1.3.6 - FINISH	
  }
  //TBIRD 1.3.5 MODS - FINISH

  //TBIRD 1.3.6 MODS - START
  if (windowName=="tv")
  {
     CFileItemPtr pItem = m_parent->m_vecItems->Get(itemNumber);      
     CPVRChannel *channel = pItem->GetPVRChannelInfoTag();
      
     if (CSettings::Get().GetBool("pvrparental.enabled")) 
     {
        if (!channel->IsLocked())
        {
           CLog::Log(LOGDEBUG, "********** bParentalLocked = False **********");           
           buttons.Add(CONTEXT_BUTTON_ADD_LOCK, 38010);
        }
        else
        {
           CLog::Log(LOGDEBUG, "########### bParentalLocked = True ###########");           
           buttons.Add(CONTEXT_BUTTON_REMOVE_LOCK, 38011);
        } 
     }  
  }
  //TBIRD 1.3.6 MODS - FINISH
}

bool CGUIWindowPVRCommon::OnContextButton(int itemNumber, CONTEXT_BUTTON button)
{
  if (itemNumber < 0 || itemNumber >= (int) m_parent->m_vecItems->Size())
    itemNumber = 0;

  CFileItemPtr pItem = m_parent->m_vecItems->Get(itemNumber);

  return //TBIRD 1.3.5 MODS - START
      	OnContextButtonUser1(pItem.get(), button) ||
	//Tuan's mods start
	OnContextButtonUser2(pItem.get(), button) ||
	//Tuan's mods finish
        OnContextButtonDelete(pItem.get(), button) ||
      	//TBIRD 1.3.5 MODS - FINISH
	    //TUAN's MODS 1.3.6 - START
		OnContextButtonDeleteAllRec(pItem.get(), button) ||
		//TUAN's MODS 1.3.6 - FINISH
        OnContextButtonFind(pItem.get(), button) ||
	OnContextButtonPVRChannels(button) ||
      	OnContextButtonPVRGuide(button) ||
      	OnContextButtonPVRRecording(button) ||
      	OnContextButtonPVRTimers(button) ||
        OnContextButtonPVRSearch(button) ||
        //TBIRD 1.3.6 MODS - START
        OnContextButtonAddLock(pItem.get(), button) ||
      	OnContextButtonRemoveLock(pItem.get(), button);
        //TBIRD 1.3.6 MODS - FINISH
      	
}
//TBIRD - STAN 1.3.5 MODS - FINISH

bool CGUIWindowPVRCommon::OnContextButtonSortByDate(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_SORTBY_DATE)
  {
    bReturn = true;

    if (m_iSortMethod != SortByDate)
    {
      m_iSortMethod = SortByDate;
      m_iSortOrder  = SortOrderAscending;
      CGUIMessage message(GUI_MSG_CHANGE_SORT_METHOD, m_parent->GetID(), 0, m_iSortMethod, 0); 
      m_parent->OnMessage(message);
    }
    else
    {
      m_iSortOrder = m_iSortOrder == SortOrderAscending ? SortOrderDescending : SortOrderAscending;
    }
    CGUIMessage message(GUI_MSG_CHANGE_SORT_DIRECTION, m_parent->GetID(), 0, m_iSortOrder, 0); 
    m_parent->OnMessage(message);
    UpdateData();
  }

  return bReturn;
}

bool CGUIWindowPVRCommon::OnContextButtonSortByName(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_SORTBY_NAME)
  {
    bReturn = true;

    if (m_iSortMethod != SortByLabel)
    {
      m_iSortMethod = SortByLabel;
      m_iSortOrder  = SortOrderAscending;
      CGUIMessage message(GUI_MSG_CHANGE_SORT_METHOD, m_parent->GetID(), 0, m_iSortMethod, 0); 
      m_parent->OnMessage(message);
    }
    else
    {
      m_iSortOrder = m_iSortOrder == SortOrderAscending ? SortOrderDescending : SortOrderAscending;
    }
    CGUIMessage message(GUI_MSG_CHANGE_SORT_DIRECTION, m_parent->GetID(), 0, m_iSortOrder, 0); 
    m_parent->OnMessage(message);
    UpdateData();
  }

  return bReturn;
}

bool CGUIWindowPVRCommon::OnContextButtonSortByChannel(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_SORTBY_CHANNEL)
  {
    bReturn = true;

    if (m_iSortMethod != SortByChannel)
    {
      m_iSortMethod = SortByChannel;
      m_iSortOrder  = SortOrderAscending;
    }
    else
    {
      m_iSortOrder = m_iSortOrder == SortOrderAscending ? SortOrderDescending : SortOrderAscending;
    }

    UpdateData();
  }

  return bReturn;
}

bool CGUIWindowPVRCommon::OnContextButtonSortAsc(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_SORTASC)
  {
    bReturn = true;

    if (m_parent->m_guiState.get())
      m_parent->m_guiState->SetNextSortOrder();
    m_parent->UpdateFileList();
  }

  return bReturn;
}

bool CGUIWindowPVRCommon::OnContextButtonSortBy(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_SORTBY)
  {
    bReturn = true;

    if (m_parent->m_guiState.get())
      m_parent->m_guiState->SetNextSortMethod();

    m_parent->UpdateFileList();
  }

  return bReturn;
}

bool CGUIWindowPVRCommon::OnContextButtonMenuHooks(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_MENU_HOOKS)
  {
    bReturn = true;

    if (item->IsEPG() && item->GetEPGInfoTag()->HasPVRChannel())
      g_PVRClients->ProcessMenuHooks(item->GetEPGInfoTag()->ChannelTag()->ClientID(), PVR_MENUHOOK_EPG, item);
    else if (item->IsPVRChannel())
      g_PVRClients->ProcessMenuHooks(item->GetPVRChannelInfoTag()->ClientID(), PVR_MENUHOOK_CHANNEL, item);
    else if (item->IsPVRRecording())
      g_PVRClients->ProcessMenuHooks(item->GetPVRRecordingInfoTag()->m_iClientId, PVR_MENUHOOK_RECORDING, item);
    else if (item->IsPVRTimer())
      g_PVRClients->ProcessMenuHooks(item->GetPVRTimerInfoTag()->m_iClientId, PVR_MENUHOOK_TIMER, item);
  }

  return bReturn;
}

//TBIRD-STAN 1.3.5 MODS - START
bool CGUIWindowPVRCommon::OnContextButtonPVRChannels(CONTEXT_BUTTON button)
{
  if (button == CONTEXT_BUTTON_PVR_CHANNELS)
  {
    CGUIMessage msgFocus(GUI_MSG_SETFOCUS,WINDOW_PVR,CONTROL_BTNCHANNELS_TV);
    g_windowManager.SendMessage(msgFocus);
    return true;
  }
  return false;
}

bool CGUIWindowPVRCommon::OnContextButtonPVRGuide(CONTEXT_BUTTON button)
{
  if (button == CONTEXT_BUTTON_PVR_GUIDE)
  {
    CGUIMessage msgFocus(GUI_MSG_SETFOCUS,WINDOW_PVR,CONTROL_BTNGUIDE);
    g_windowManager.SendMessage(msgFocus);
    return true;
  }
  return false;
}

bool CGUIWindowPVRCommon::OnContextButtonPVRRecording(CONTEXT_BUTTON button)
{
  if (button == CONTEXT_BUTTON_PVR_RECORDING)
  {
    //(-/+)TBIRD 1.3.6 MODS - START	
    CGUIMessage msgFocus1(GUI_MSG_SETFOCUS,WINDOW_PVR,CONTROL_BTNRECORDINGS);
    g_windowManager.SendMessage(msgFocus1);    
    CGUIMessage msgFocus2(GUI_MSG_SETFOCUS,WINDOW_PVR, CONTROL_LIST_RECORDINGS);
    g_windowManager.SendMessage(msgFocus2);
    //(-/+)TBIRD 1.3.6 MODS - FINISH
    return true;
  }
  return false;
}

bool CGUIWindowPVRCommon::OnContextButtonPVRTimers(CONTEXT_BUTTON button)
{
  if (button == CONTEXT_BUTTON_PVR_TIMERS)
  {
    CGUIMessage msgFocus(GUI_MSG_SETFOCUS,WINDOW_PVR,CONTROL_BTNTIMERS);
    g_windowManager.SendMessage(msgFocus);
    return true;
  }
  return false;
}

bool CGUIWindowPVRCommon::OnContextButtonPVRSearch(CONTEXT_BUTTON button)
{
  if (button == CONTEXT_BUTTON_PVR_SEARCH)
  {
    // CGUIMessage msgFocus(GUI_MSG_SETFOCUS,WINDOW_PVR,CONTROL_BTNSEARCH);
    // ////  g_windowManager.SendMessage(msgFocus);
    CGUIMessage msg(GUI_MSG_CLICKED,CONTROL_BTNSEARCH,WINDOW_PVR);
    g_windowManager.SendMessage(msg); 
    return true;
  }
  return false;
}
//TBIRD - STAN 1.3.5 MODS - FINISH

//TBIRD 1.3.5 MODS - START
bool CGUIWindowPVRCommon::OnContextButtonUser1(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_USER1)
  {
      StartRecordAll();
      bReturn = true;
  }
 	
  return bReturn;
}

//Tuan's mod start
bool CGUIWindowPVRCommon::OnContextButtonUser2(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_USER2)
  {
	  StopRecordAll();
      bReturn = true;
  }
  return bReturn;
}
//Tuan's mod finish

bool CGUIWindowPVRCommon::OnContextButtonDelete(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_DELETE)
  {
      CLog::Log(LOGDEBUG, "CGUIWindowPVRCommon - OnContextButtonDelete");
      ActionDeleteRecording(item);
      bReturn = true;
  }
 	
  return bReturn;
}
//TBIRD 1.3.5 MODS - FINISH

//TUAN's MODS 1.3.6 - START
bool CGUIWindowPVRCommon::OnContextButtonDeleteAllRec(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_DELETE_ALL_REC)
  {
      CLog::Log(LOGDEBUG, "CGUIWindowPVRCommon - %s",__FUNCTION__);
	  ActionDeleteAllRecordings(item);
      bReturn = true;
  }
 	
  return bReturn;
}
//TUAN's MODS 1.3.6 - FINISH

bool CGUIWindowPVRCommon::ActionDeleteTimer(CFileItem *item)
{
  /* check if the timer tag is valid */
  CPVRTimerInfoTag *timerTag = item->GetPVRTimerInfoTag();
  if (!timerTag || timerTag->m_iClientIndex < 0)
    return false;

  /* show a confirmation dialog */
  CGUIDialogYesNo* pDialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
  if (!pDialog)
    return false;
  pDialog->SetHeading(122);
  pDialog->SetLine(0, 19040);
  pDialog->SetLine(1, "");
  pDialog->SetLine(2, timerTag->m_strTitle);
  pDialog->DoModal();

  /* prompt for the user's confirmation */
  if (!pDialog->IsConfirmed())
    return false;

  /* delete the timer */
  return g_PVRTimers->DeleteTimer(*item);
}

bool CGUIWindowPVRCommon::ShowNewTimerDialog(void)
{
  bool bReturn(false);

  CPVRTimerInfoTag *newTimer = new CPVRTimerInfoTag;
  CFileItem *newItem = new CFileItem(*newTimer);
  if (ShowTimerSettings(newItem))
  {
    /* Add timer to backend */
    bReturn = g_PVRTimers->AddTimer(*newItem->GetPVRTimerInfoTag());
  }

  delete newItem;
  delete newTimer;

  return bReturn;
}

bool CGUIWindowPVRCommon::ActionShowTimer(CFileItem *item)
{
  bool bReturn = false;

  /* Check if "Add timer..." entry is pressed by OK, if yes
     create a new timer and open settings dialog, otherwise
     open settings for selected timer entry */
  if (item->GetPath() == "pvr://timers/add.timer")
  {
    bReturn = ShowNewTimerDialog();
  }
  else
  {
    if (ShowTimerSettings(item))
    {
      /* Update timer on pvr backend */
      bReturn = g_PVRTimers->UpdateTimer(*item);
    }
  }

  return bReturn;
}

bool CGUIWindowPVRCommon::ActionRecord(CFileItem *item)
{
  bool bReturn = false;

  CEpgInfoTag *epgTag = item->GetEPGInfoTag();
  if (!epgTag)
    return bReturn;

  CPVRChannelPtr channel = epgTag->ChannelTag();
  if (!channel || !g_PVRManager.CheckParentalLock(*channel))
    return bReturn;

  if (epgTag->Timer() == NULL)
  {
    /* create a confirmation dialog */
    CGUIDialogYesNo* pDialog = (CGUIDialogYesNo*) g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
    if (!pDialog)
      return bReturn;

    pDialog->SetHeading(264);
    pDialog->SetLine(0, "");
    pDialog->SetLine(1, epgTag->Title());
    pDialog->SetLine(2, "");
    pDialog->DoModal();

    /* prompt for the user's confirmation */
    if (!pDialog->IsConfirmed())
      return bReturn;

    CPVRTimerInfoTag *newTimer = CPVRTimerInfoTag::CreateFromEpg(*epgTag);
    if (newTimer)
    {
      bReturn = g_PVRTimers->AddTimer(*newTimer);
      delete newTimer;
    }
    else
    {
      bReturn = false;
    }
  }
  else
  {
    CGUIDialogOK::ShowAndGetInput(19033,19034,0,0);
    bReturn = true;
  }

  return bReturn;
}


bool CGUIWindowPVRCommon::ActionDeleteRecording(CFileItem *item)
{
  bool bReturn = false;

  /* check if the recording tag is valid */
  CPVRRecording *recTag = (CPVRRecording *) item->GetPVRRecordingInfoTag();
  if (!recTag || recTag->m_strRecordingId.empty())
    return bReturn;

  /* show a confirmation dialog */
  CGUIDialogYesNo* pDialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
  if (!pDialog)
    return bReturn;
  pDialog->SetHeading(122);
  pDialog->SetLine(0, 19043);
  pDialog->SetLine(1, "");
  pDialog->SetLine(2, recTag->m_strTitle);
  pDialog->DoModal();

  /* prompt for the user's confirmation */
  if (!pDialog->IsConfirmed())
    return bReturn;

  /* delete the recording */
  if (g_PVRRecordings->DeleteRecording(*item))
  {
    g_PVRManager.TriggerRecordingsUpdate();
    bReturn = true;
  }

  return bReturn;
}

//TUAN's MODS 1.3.6 - START
void CGUIWindowPVRCommon::ActionDeleteAllRecordings(CFileItem *item)
{

  CLog::Log(LOGDEBUG, "CGUIWindowPVRCommon - %s",__FUNCTION__);
  int totalItem =  m_parent->m_vecItems->Size(); 

  //ADDING A CONFIRM DIALOG 	
  /* show a confirmation dialog */
  CGUIDialogYesNo* pDialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
  if (!pDialog)
    return; 
  pDialog->SetHeading(122);
  pDialog->SetLine(0, 37107);
  pDialog->SetLine(1, 36055);
  pDialog->SetLine(2, "");
  pDialog->DoModal();

  /* prompt for the user's confirmation */
  if (!pDialog->IsConfirmed())
    return; 

  if (totalItem > 0) 	
  {

		bool finishAll = true;
		for (int i=0; i < m_parent->m_vecItems->Size(); i++)
		{ 
	    	CFileItemPtr pItem = m_parent->m_vecItems->Get(i);
			CLog::Log(LOGDEBUG, "CGUIWindowPVRCommon - %s: path to item = %s",__FUNCTION__, pItem.get()->GetPath().c_str());
		
			if (!ActionDeleteRecordingInAll(pItem.get()))
			{
				 finishAll = false;				
				 CLog::Log(LOGDEBUG, "CGUIWindowPVRCommon - %s - iItem %d failed to record", __FUNCTION__, i); 
			}
  		}	
	    g_PVRManager.TriggerRecordingsUpdate(); //Refresh recording window

  	    if (!finishAll)	
	    {	
	 		CGUIDialogOK::ShowAndGetInput(19033,37103,37104,37105); /* print info */
		}
		else				
	    {	
	 		CGUIDialogOK::ShowAndGetInput(19033,37106,0,0); /* print info */
		}
  } 
  else
  {
		//PRINT OUT SOMETHING SAYING NO RECORDING TO DELETE
	    CGUIDialogOK::ShowAndGetInput(19033,37102,0,0); /* print info */
  }  
}

bool CGUIWindowPVRCommon::ActionDeleteRecordingInAll(CFileItem *item)
{
  bool bReturn = false;

  /* check if the recording tag is valid */
  CPVRRecording *recTag = (CPVRRecording *) item->GetPVRRecordingInfoTag();
  if (!recTag || recTag->m_strRecordingId.empty())
    return bReturn;

  /* delete the recording */
  if (g_PVRRecordings->DeleteRecordingQuiet(*item))
	    bReturn = true;

  return bReturn;
}
//TUAN's MODS 1.3.6 - FINISH

bool CGUIWindowPVRCommon::ActionPlayChannel(CFileItem *item)
{
  bool bReturn = false;

  if (item->GetPath() == "pvr://channels/.add.channel")
  {
    /* show "add channel" dialog */
    CGUIDialogOK::ShowAndGetInput(19033,0,19038,0);
    bReturn = true;
  }
  else
  {
    /* open channel depending on the parental lock */
    //(-/+) TBIRD 1.3.6 MODS - START
    CPVRChannel *channel = item->GetPVRChannelInfoTag();

    if (!channel->IsLocked())
    {
       bReturn = PlayFile(item, CSettings::Get().GetBool("pvrplayback.playminimized"));
    }
    else
    {
       if (g_PVRManager.CheckParentalPIN(g_localizeStrings.Get(19262).c_str()))
       {
          bReturn = PlayFile(item, CSettings::Get().GetBool("pvrplayback.playminimized"));          
       }
    }
    //(-/+) TBIRD 1.3.6 MODS - FINISH
    
  }

  return bReturn;
}

bool CGUIWindowPVRCommon::ActionPlayEpg(CFileItem *item)
{
  CPVRChannelPtr channel;
  if (item && item->HasEPGInfoTag() && item->GetEPGInfoTag()->HasPVRChannel())
    channel = item->GetEPGInfoTag()->ChannelTag();
  
  if (!channel || !g_PVRManager.CheckParentalLock(*channel))
    return false;
  
  CFileItem channelItem = CFileItem(*channel);
  g_application.SwitchToFullScreen();
  if (!PlayFile(&channelItem))
  {
    // CHANNELNAME could not be played. Check the log for details.
    CStdString msg = StringUtils::Format(g_localizeStrings.Get(19035).c_str(), channel->ChannelName().c_str());
    CGUIDialogOK::ShowAndGetInput(19033, 0, msg, 0);
    return false;
  }
  
  return true;
}

bool CGUIWindowPVRCommon::ActionDeleteChannel(CFileItem *item)
{
  CPVRChannel *channel = item->GetPVRChannelInfoTag();

  /* check if the channel tag is valid */
  if (!channel || channel->ChannelNumber() <= 0)
    return false;

  /* show a confirmation dialog */
  CGUIDialogYesNo* pDialog = (CGUIDialogYesNo*) g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
  if (!pDialog)
    return false;
  pDialog->SetHeading(19039);
  pDialog->SetLine(0, "");
  pDialog->SetLine(1, channel->ChannelName());
  pDialog->SetLine(2, "");
  pDialog->DoModal();

  /* prompt for the user's confirmation */
  if (!pDialog->IsConfirmed())
    return false;

  g_PVRChannelGroups->GetGroupAll(channel->IsRadio())->RemoveFromGroup(*channel);
  UpdateData();

  return true;
}

bool CGUIWindowPVRCommon::UpdateEpgForChannel(CFileItem *item)
{
  CPVRChannel *channel = item->GetPVRChannelInfoTag();
  CEpg *epg = channel->GetEPG();
  if (!epg)
    return false;

  epg->ForceUpdate();
  return true;
}

bool CGUIWindowPVRCommon::ShowTimerSettings(CFileItem *item)
{
  /* Check item is TV timer information tag */
  if (!item->IsPVRTimer())
  {
    CLog::Log(LOGERROR, "CGUIWindowPVRTimers: Can't open timer settings dialog, no timer info tag!");
    return false;
  }

  /* Load timer settings dialog */
  CGUIDialogPVRTimerSettings* pDlgInfo = (CGUIDialogPVRTimerSettings*)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_TIMER_SETTING);

  if (!pDlgInfo)
    return false;

  /* inform dialog about the file item */
  pDlgInfo->SetTimer(item);

  /* Open dialog window */
  pDlgInfo->DoModal();

  /* Get modify flag from window and return it to caller */
  return pDlgInfo->GetOK();
}


bool CGUIWindowPVRCommon::PlayRecording(CFileItem *item, bool bPlayMinimized /* = false */)
{
  if (!item->HasPVRRecordingInfoTag())
    return false;

  CStdString stream = item->GetPVRRecordingInfoTag()->m_strStreamURL;
  if (stream == "")
  {
    CApplicationMessenger::Get().PlayFile(*item, false);
    return true;
  }

  /* Isolate the folder from the filename */
  size_t found = stream.find_last_of("/");
  if (found == CStdString::npos)
    found = stream.find_last_of("\\");

  if (found != CStdString::npos)
  {
    /* Check here for asterisk at the begin of the filename */
    if (stream[found+1] == '*')
    {
      /* Create a "stack://" url with all files matching the extension */
      CStdString ext = URIUtils::GetExtension(stream);
      CStdString dir = stream.substr(0, found).c_str();

      CFileItemList items;
      CDirectory::GetDirectory(dir, items);
      items.Sort(SortByFile, SortOrderAscending);

      vector<int> stack;
      for (int i = 0; i < items.Size(); ++i)
      {
        if (URIUtils::HasExtension(items[i]->GetPath(), ext))
          stack.push_back(i);
      }

      if (stack.size() > 0)
      {
        /* If we have a stack change the path of the item to it */
        CStackDirectory dir;
        CStdString stackPath = dir.ConstructStackPath(items, stack);
        item->SetPath(stackPath);
      }
    }
    else
    {
      /* If no asterisk is present play only the given stream URL */
      item->SetPath(stream);
    }
  }
  else
  {
    CLog::Log(LOGERROR, "CGUIWindowPVRCommon - %s - can't open recording: no valid filename", __FUNCTION__);
    CGUIDialogOK::ShowAndGetInput(19033,0,19036,0);
    return false;
  }

  CApplicationMessenger::Get().PlayFile(*item, false);

  return true;
}

bool CGUIWindowPVRCommon::PlayFile(CFileItem *item, bool bPlayMinimized /* = false */)
{
  if (item->m_bIsFolder)
  {
    return false;
  }

  if (item->GetPath() == g_application.CurrentFile())
  {
    CGUIMessage msg(GUI_MSG_FULLSCREEN, 0, m_parent->GetID());
    g_windowManager.SendMessage(msg);
    return true;
  }

  CMediaSettings::Get().SetVideoStartWindowed(bPlayMinimized);

  if (item->HasPVRRecordingInfoTag())
  {
    return PlayRecording(item, bPlayMinimized);
  }
  else
  {
    bool bSwitchSuccessful(false);

    CPVRChannel *channel = item->HasPVRChannelInfoTag() ? item->GetPVRChannelInfoTag() : NULL;

    if (channel && g_PVRManager.CheckParentalLock(*channel))
    {
      /* try a fast switch */
      if (channel && (g_PVRManager.IsPlayingTV() || g_PVRManager.IsPlayingRadio()) &&
         (channel->IsRadio() == g_PVRManager.IsPlayingRadio()))
      {
        if (channel->StreamURL().empty())
          bSwitchSuccessful = g_application.m_pPlayer->SwitchChannel(*channel);
      }

      if (!bSwitchSuccessful)
      {
        CApplicationMessenger::Get().PlayFile(*item, false);
        return true;
      }
    }

    if (!bSwitchSuccessful)
    {
      CStdString channelName = g_localizeStrings.Get(19029); // Channel
      if (channel)
        channelName = channel->ChannelName();
      CStdString msg = StringUtils::Format(g_localizeStrings.Get(19035).c_str(), channelName.c_str()); // CHANNELNAME could not be played. Check the log for details.

      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error,
              g_localizeStrings.Get(19166), // PVR information
              msg);
      return false;
    }
  }

  return true;
}

bool CGUIWindowPVRCommon::StartRecordFile(CFileItem *item)
{
  if (!item->HasEPGInfoTag())
    return false;

  CEpgInfoTag *tag = item->GetEPGInfoTag();
  CPVRChannelPtr channel;
  if (tag)
    channel = tag->ChannelTag();

  if (!channel || !g_PVRManager.CheckParentalLock(*channel))
    return false;

  CFileItemPtr timer = g_PVRTimers->GetTimerForEpgTag(item);
  if (timer && timer->HasPVRTimerInfoTag())
  {
    CGUIDialogOK::ShowAndGetInput(19033,19034,0,0);
    return false;
  }

  CGUIDialogYesNo* pDialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
  if (!pDialog)
    return false;
  pDialog->SetHeading(264);
  pDialog->SetLine(0, tag->PVRChannelName());
  pDialog->SetLine(1, "");
  pDialog->SetLine(2, tag->Title());
  pDialog->DoModal();

  if (!pDialog->IsConfirmed())
    return false;

  CPVRTimerInfoTag *newTimer = CPVRTimerInfoTag::CreateFromEpg(*tag);
  bool bReturn(false);
  if (newTimer)
  {
    bReturn = g_PVRTimers->AddTimer(*newTimer);
    delete newTimer;
  }
  return bReturn;
}
//TGD 1.3.5 MODS -START
/**
 * Utilise StartRecordFile method causes the problem of multiple YES/NO dialogues.
 * Implement StartRecordFileInAll with similar functionality except for 
 */
bool CGUIWindowPVRCommon::StartRecordFileInAll(CFileItem *item)
{
  if (!item->HasEPGInfoTag())
    return false;

  CEpgInfoTag *tag = item->GetEPGInfoTag();
  CPVRChannelPtr channel;
  if (tag)
    channel = tag->ChannelTag();

  CLog::Log(LOGDEBUG, "== TGD == CGUIWindowPVRCommon - %s - Tag channel successfully!",__FUNCTION__); 

  if (!channel || !g_PVRManager.CheckParentalLock(*channel))
    return false;

  CLog::Log(LOGDEBUG, "== TGD == CGUIWindowPVRCommon - %s - No Parental Lock -> Move on",__FUNCTION__); 

  CFileItemPtr timer = g_PVRTimers->GetTimerForEpgTag(item);

  CLog::Log(LOGDEBUG, "== TGD == CGUIWindowPVRCommon - %s - Successfully initialising a timer",__FUNCTION__);  

  if (timer && timer->HasPVRTimerInfoTag())
  {
	//The "already-recorded" message should be removed. 
    //It becomes a problem when select "Record All" again after timers are created -> multiple messages! 
    //CGUIDialogOK::ShowAndGetInput(19033,19034,0,0);
	CLog::Log(LOGDEBUG, "== TGD == CGUIWindowPVRCommon - %s - Timer already scheduled!",__FUNCTION__); 
    return false;
  }

  //This Yes/No dialogue code will be removed! Only keep it here or debugging reference
  /*
  CGUIDialogYesNo* pDialog = (CGUIDialogYesNo*)g_windowManager.GetWindow(WINDOW_DIALOG_YES_NO);
  if (!pDialog)
    return false;
  pDialog->SetHeading(264);
  pDialog->SetLine(0, tag->PVRChannelName());
  pDialog->SetLine(1, "");
  pDialog->SetLine(2, tag->Title());
  pDialog->DoModal();
  if (!pDialog->IsConfirmed())
    return false;
  */

  CLog::Log(LOGDEBUG, "== TGD == CGUIWindowPVRCommon - %s - About to initalise a new Timer from Epg",__FUNCTION__); 
  CPVRTimerInfoTag *newTimer = CPVRTimerInfoTag::CreateFromEpg(*tag);
  bool bReturn(false);
  if (newTimer)
  {
    CLog::Log(LOGDEBUG, "== TGD == CGUIWindowPVRCommon - %sl - New Timer Initialised",__FUNCTION__); 
    bReturn = g_PVRTimers->AddTimer(*newTimer);
    delete newTimer;
  }
  return bReturn;
}

void CGUIWindowPVRCommon::StopRecordAll()
{
  bool finish = false;
  int  count  = 0;	

  while((!finish) && (count < 10) )	/* Set limit to avoid inifinite loop*/
  {
	  int iItem = m_parent->m_viewControl.GetSelectedItem();	

	  /*Assume all will be deleted. Increase count. */
	  finish = true;
	  count++;
	
	  for (int i=0; i < m_parent->m_vecItems->Size(); i++)
	  { 
			 CFileItemPtr pItem = m_parent->m_vecItems->Get(i);
			 if (!StopRecordFileInAll(pItem.get()))
			 {
				 CLog::Log(LOGDEBUG, "=== TGD === CGUIWindowPVRCommon - %s - Recording/Timer %d failed to to be stopped!", __FUNCTION__, i); 
				 finish = false; /*Nope! one failed! Do it all over again */
			 }
	  }
  }	
}
//Tuan's mods fininsh

//TGD 1.3.5 MODS -FINISH
//TBIRD 1.3.5 MODS - START
void CGUIWindowPVRCommon::StartRecordAll()
{
  int iItem = m_parent->m_viewControl.GetSelectedItem();
    
  CLog::Log(LOGDEBUG, "CGUIWindowPVRCommon - StartRecordAll - Value of iItem = %d", iItem);
  //for loop
  for (int i=0; i < m_parent->m_vecItems->Size(); i++)
  { 
	     CFileItemPtr pItem = m_parent->m_vecItems->Get(i);
		 //TGD 1.3.5 MODS -START
		 //StartRecordFile(pItem.get());
		 if (!StartRecordFileInAll(pItem.get()))
		 {
			 CLog::Log(LOGDEBUG, "=== TGD === CGUIWindowPVRCommon - StartRecordAll - iItem %d failed to record", i); 
		 }
	     //TGD 1.3.5 MODS -FINISH
  }
}
//TBIRD 1.3.5 MODS - FINISH

//Tuan's mods start
bool CGUIWindowPVRCommon::StopRecordFileInAll(CFileItem *item)
{
	//return StopRecordFile(item); /* This line should be the only line in this method */
  if (!item->HasEPGInfoTag())
  { 
	 CLog::Log(LOGDEBUG, "=== TGD === CGUIWindowPVRCommon - %s - item doesnt have EPGInfoTag - Cant delete timer!", __FUNCTION__); 
	 return false;
  }

  CEpgInfoTag *tag = item->GetEPGInfoTag();
  if (!tag || !tag->HasPVRChannel())
  { 
	 CLog::Log(LOGDEBUG, "=== TGD === CGUIWindowPVRCommon - %s - tag doesnt have PVRChannel - Cant delete timer!", __FUNCTION__); 
	 return false;
  }	

  CFileItemPtr timer = g_PVRTimers->GetTimerForEpgTag(item);
  if (!timer || !timer->HasPVRTimerInfoTag() || timer->GetPVRTimerInfoTag()->m_bIsRepeating)
  { 	
	CLog::Log(LOGDEBUG, "=== TGD === CGUIWindowPVRCommon - %s - timer not exist or hasPVRTimerInfoTag - Cant delete timer!", __FUNCTION__); 
    return false;
  }	

  //Force delete! Return no PVR error message (which is unnecessarily to users)! Still keep YES/NO confirm to delete.
  bool bReturn = g_PVRTimers->DeleteTimerQuiet(*timer, true); 
  if (!bReturn)
  {
	  CLog::Log(LOGDEBUG, "=== TGD === CGUIWindowPVRCommon - %s - PVR client failed to delete timer! ", __FUNCTION__); 
  }	
  return bReturn;	
}
//Tuan's mods finish

//TBIRD 1.3.6 MODS - START
bool CGUIWindowPVRCommon::OnContextButtonAddLock(CFileItem *Item, CONTEXT_BUTTON button)
{ 
   int iItem = m_parent->m_viewControl.GetSelectedItem();
   
   if (iItem < 0)
      iItem = 0;

   CEpgInfoTag *epgtag = Item->GetEPGInfoTag();

   CPVRChannel *channel = Item->GetPVRChannelInfoTag();

   CLog::Log(LOGDEBUG, "=== TBIRD === Channel Number = %d ", channel->ChannelNumber());

   if (button == CONTEXT_BUTTON_ADD_LOCK)
   {   
      if (!channel || channel->ChannelNumber() <= 0)
      {
         CGUIDialogOK::ShowAndGetInput(19033,0,37012,0);
         return false;
      }
   
      if (g_PVRManager.CheckParentalPIN(g_localizeStrings.Get(19262).c_str()))
      {
         channel->SetLocked(true);          
         CEpgContainer::Get().UpdateEPG(true); 
         m_parent->m_viewControl.SetFocused();
         return true;
      }  
   }
}

bool CGUIWindowPVRCommon::OnContextButtonRemoveLock(CFileItem *Item, CONTEXT_BUTTON button)
{
   int iItem = m_parent->m_viewControl.GetSelectedItem();

   if (iItem < 0)
      iItem = 0;

   CEpgInfoTag *epgtag = Item->GetEPGInfoTag();
   
   if (button == CONTEXT_BUTTON_REMOVE_LOCK)
   {
      CPVRChannel *channel = Item->GetPVRChannelInfoTag();

      CLog::Log(LOGDEBUG, "=== TBIRD === Channel Number = %d ", channel->ChannelNumber()); 

      if (!channel || channel->ChannelNumber() <= 0)
      {
         CGUIDialogOK::ShowAndGetInput(19033,0,37013,0);
         return false;
      }
      
      if (g_PVRManager.CheckParentalPIN(g_localizeStrings.Get(19262).c_str()))
      {
         channel->SetLocked(false); 
         epgtag->Title(true);          
         CEpgContainer::Get().UpdateEPG(true);
         m_parent->m_viewControl.SetFocused();
         return true;          
      }         
   } 
}
//TBIRD 1.3.6 MODS - FINISH

bool CGUIWindowPVRCommon::StopRecordFile(CFileItem *item)
{
  if (!item->HasEPGInfoTag())
    return false;

  CEpgInfoTag *tag = item->GetEPGInfoTag();
  if (!tag || !tag->HasPVRChannel())
    return false;

  CFileItemPtr timer = g_PVRTimers->GetTimerForEpgTag(item);
  if (!timer || !timer->HasPVRTimerInfoTag() || timer->GetPVRTimerInfoTag()->m_bIsRepeating)
    return false;

  return g_PVRTimers->DeleteTimer(*timer);
}

void CGUIWindowPVRCommon::ShowEPGInfo(CFileItem *item)
{
  CFileItem *tag = NULL;
  bool bHasChannel(false);
  CPVRChannel channel;
  if (item->IsEPG())
  {
    tag = new CFileItem(*item);
    if (item->GetEPGInfoTag()->HasPVRChannel())
    {
      channel = *item->GetEPGInfoTag()->ChannelTag();
      bHasChannel = true;
    }
  }
  else if (item->IsPVRChannel())
  {
    CEpgInfoTag epgnow;
    channel = *item->GetPVRChannelInfoTag();
    bHasChannel = true;
    if (!item->GetPVRChannelInfoTag()->GetEPGNow(epgnow))
    {
      CGUIDialogOK::ShowAndGetInput(19033,0,19055,0);
      return;
    }
    tag = new CFileItem(epgnow);
  }

  if (tag)
  {
    if (!bHasChannel || g_PVRManager.CheckParentalLock(channel))
    {
      CGUIDialogPVRGuideInfo* pDlgInfo = (CGUIDialogPVRGuideInfo*)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_GUIDE_INFO);
      if (pDlgInfo)
      {
        pDlgInfo->SetProgInfo(tag);
        pDlgInfo->DoModal();
      }
    }
    delete tag;
  }
}

void CGUIWindowPVRCommon::ShowRecordingInfo(CFileItem *item)
{
  if (!item->IsPVRRecording())
    return;

  CGUIDialogPVRRecordingInfo* pDlgInfo = (CGUIDialogPVRRecordingInfo*)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_RECORDING_INFO);
  if (!pDlgInfo)
    return;

  pDlgInfo->SetRecording(item);
  pDlgInfo->DoModal();
}

bool CGUIWindowPVRCommon::OnContextButtonFind(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_FIND)
  {
    //TBIRD 1.3.6 MODS - START
    int iItem = m_parent->m_viewControl.GetSelectedItem();

    if (iItem < 0)
       return false;

    //TBIRD 1.3.6 MODS - FINISH

    //(-/+) TBIRD-STAN MODS 1.3.5 - START
    CStdString clientName;
    CPVRChannel *channel = item->GetPVRChannelInfoTag();
    g_PVRClients->GetClientName(channel->ClientID(), clientName);
    
    CLog::Log(LOGDEBUG, "CGUIWindowPVRCommon -PVR Client Name =  %s ", clientName.c_str());

    if (clientName != "IPTV Simple PVR Add-on:connected")
    {
	    bReturn = true;
	    if (m_parent->m_windowSearch)
	    {
	      CEpgInfoTag tag;
	      m_parent->m_windowSearch->m_searchfilter.Reset();
	      if (item->IsEPG())
		m_parent->m_windowSearch->m_searchfilter.m_strSearchTerm = "\"" + item->GetEPGInfoTag()->Title() + "\"";
	      else if (item->IsPVRChannel() && item->GetPVRChannelInfoTag()->GetEPGNow(tag))
		m_parent->m_windowSearch->m_searchfilter.m_strSearchTerm = "\"" + tag.Title() + "\"";
	      else if (item->IsPVRRecording())
		m_parent->m_windowSearch->m_searchfilter.m_strSearchTerm = "\"" + item->GetPVRRecordingInfoTag()->m_strTitle + "\"";

	      m_parent->m_windowSearch->m_bSearchConfirmed = true;
	      m_parent->SetLabel(m_iControlButton, 0);
	      m_parent->SetActiveView(m_parent->m_windowSearch);
	      m_parent->m_windowSearch->UpdateData();
	      m_parent->SetLabel(m_iControlList, 0);
	      m_parent->m_viewControl.SetFocused();    
	    }
     }     
     //(-/+) TBIRD-STAN MODS 1.3.5 - FINISH
     //TBIRD 1.3.6 MODS - START
     else
     {
        CGUIDialogOK::ShowAndGetInput(19033,0,37008,0);
        return true; 
     } 
     //TBIRD 1.3.6 MODS - FINISH
  }

  return bReturn;
}

void CGUIWindowPVRCommon::ShowBusyItem(void)
{
  // FIXME: display a temporary entry so that the list can keep its focus
  // busy_items has to be static, because m_viewControl holds the pointer to it
  static CFileItemList busy_items;
  if (busy_items.IsEmpty())
  {
    CFileItemPtr pItem(new CFileItem(g_localizeStrings.Get(1040)));
    busy_items.AddFront(pItem, 0);
  }
  m_parent->m_viewControl.SetItems(busy_items);
}
