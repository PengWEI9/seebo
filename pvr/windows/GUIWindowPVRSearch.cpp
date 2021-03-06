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

#include "GUIWindowPVRSearch.h"

#include "dialogs/GUIDialogOK.h"
#include "dialogs/GUIDialogProgress.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/Key.h"
#include "pvr/PVRManager.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "pvr/dialogs/GUIDialogPVRGuideSearch.h"
#include "epg/EpgContainer.h"
#include "pvr/recordings/PVRRecordings.h"
#include "GUIWindowPVR.h"
#include "utils/log.h"
#include "pvr/addons/PVRClients.h"

using namespace PVR;
using namespace EPG;

CGUIWindowPVRSearch::CGUIWindowPVRSearch(CGUIWindowPVR *parent) :
  CGUIWindowPVRCommon(parent, PVR_WINDOW_SEARCH, CONTROL_BTNSEARCH, CONTROL_LIST_SEARCH),
  m_bSearchStarted(false),
  m_bSearchConfirmed(false)
{
}

void CGUIWindowPVRSearch::GetContextButtons(int itemNumber, CContextButtons &buttons) const
{
  if (itemNumber < 0 || itemNumber >= m_parent->m_vecItems->Size())
    return;
  CFileItemPtr pItem = m_parent->m_vecItems->Get(itemNumber);
  
  //TBIRD-STAN 1.3.5 MODS - START
  //TBIRD 1.3.4 MODS - START  
  /*buttons.Add(CONTEXT_BUTTON_USER1, 36044);
  buttons.Add(CONTEXT_BUTTON_USER2, 36045);
  buttons.Add(CONTEXT_BUTTON_USER3, 36046);
  buttons.Add(CONTEXT_BUTTON_USER4, 36047);
  buttons.Add(CONTEXT_BUTTON_USER5, 36048);  //Search option*/
  //TBIRD 1.3.4 MODS - FINISH
  //TBIRD-STAN 1.3.5 MODS - FINISH
}

bool CGUIWindowPVRSearch::OnContextButton(int itemNumber, CONTEXT_BUTTON button)
{
  if (itemNumber < 0 || itemNumber >= m_parent->m_vecItems->Size())
    return false;
  CFileItemPtr pItem = m_parent->m_vecItems->Get(itemNumber);

  return OnContextButtonClear(pItem.get(), button) ||
      OnContextButtonInfo(pItem.get(), button) ||
      OnContextButtonStopRecord(pItem.get(), button) ||
      OnContextButtonStartRecord(pItem.get(), button) ||
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

void CGUIWindowPVRSearch::UpdateData(bool bUpdateSelectedFile /* = true */)
{
  CLog::Log(LOGDEBUG, "CGUIWindowPVRSearch - %s - update window '%s'. set view to %d", __FUNCTION__, GetName(), m_iControlList);
  m_bUpdateRequired = false;

  /* lock the graphics context while updating */
  CSingleLock graphicsLock(g_graphicsContext);

  m_iSelected = m_parent->m_viewControl.GetSelectedItem();
  m_parent->m_viewControl.Clear();
  m_parent->m_vecItems->Clear();
  m_parent->m_viewControl.SetCurrentView(m_iControlList);

  if (m_bSearchConfirmed)
  {
    CGUIDialogProgress* dlgProgress = (CGUIDialogProgress*)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
    if (dlgProgress)
    {
      dlgProgress->SetHeading(194);
      dlgProgress->SetLine(0, m_searchfilter.m_strSearchTerm);
      dlgProgress->SetLine(1, "");
      dlgProgress->SetLine(2, "");
      dlgProgress->StartModal();
      dlgProgress->Progress();
    }

    // TODO get this from the selected channel group
    g_EpgContainer.GetEPGSearch(*m_parent->m_vecItems, m_searchfilter);
    if (dlgProgress)
      dlgProgress->Close();

    if (m_parent->m_vecItems->Size() == 0)
    {
      CGUIDialogOK::ShowAndGetInput(194, 284, 0, 0);
      m_bSearchConfirmed = false;
    }
  }

  if (m_parent->m_vecItems->Size() == 0)
  {
    CFileItemPtr item;
    item.reset(new CFileItem("pvr://guide/searchresults/empty.epg", false));
    item->SetLabel(g_localizeStrings.Get(19027));
    item->SetLabelPreformated(true);
    m_parent->m_vecItems->Add(item);
  }
  else
  {
    m_parent->m_vecItems->Sort(m_iSortMethod, m_iSortOrder, m_iSortAttributes);
  }

  m_parent->m_viewControl.SetItems(*m_parent->m_vecItems);

  if (bUpdateSelectedFile)
    m_parent->m_viewControl.SetSelectedItem(m_iSelected);

  m_parent->SetLabel(CONTROL_LABELHEADER, g_localizeStrings.Get(283));
  m_parent->SetLabel(CONTROL_LABELGROUP, "");
}

bool CGUIWindowPVRSearch::OnClickButton(CGUIMessage &message)
{
  bool bReturn = false;

  if (IsSelectedButton(message))
  {
    bReturn = true;
    ShowSearchResults();
  }

  return bReturn;
}

bool CGUIWindowPVRSearch::OnClickList(CGUIMessage &message)
{
  bool bReturn = false;

  if (IsSelectedList(message))
  {
    bReturn = true;
    int iAction = message.GetParam1();
    int iItem = m_parent->m_viewControl.GetSelectedItem();

    /* get the fileitem pointer */
    if (iItem < 0 || iItem >= m_parent->m_vecItems->Size())
      return bReturn;
    CFileItemPtr pItem = m_parent->m_vecItems->Get(iItem);

    /* process actions */
    if (iAction == ACTION_SHOW_INFO || iAction == ACTION_SELECT_ITEM || iAction == ACTION_MOUSE_LEFT_CLICK)
      ActionShowSearch(pItem.get());
    else if (iAction == ACTION_CONTEXT_MENU || iAction == ACTION_MOUSE_RIGHT_CLICK)
      m_parent->OnPopupMenu(iItem);
    else if (iAction == ACTION_RECORD)
      ActionRecord(pItem.get());
  }

  return bReturn;
}

bool CGUIWindowPVRSearch::OnContextButtonClear(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_CLEAR)
  {
    bReturn = true;

    m_bSearchStarted = false;
    m_bSearchConfirmed = false;
    m_searchfilter.Reset();

    UpdateData();
  }

  return bReturn;
}

bool CGUIWindowPVRSearch::OnContextButtonInfo(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_INFO)
  {
    bReturn = true;

    ShowEPGInfo(item);
  }

  return bReturn;
}

bool CGUIWindowPVRSearch::OnContextButtonStartRecord(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_START_RECORD)
  {
    bReturn = true;

    StartRecordFile(item);
  }

  return bReturn;
}

//TBIRD 1.3.5 MODS - START
void CGUIWindowPVRSearch::RecordAll()
{
  int iItem = m_parent->m_viewControl.GetSelectedItem();
    
  //for loop
  for (int i=iItem; m_parent->m_vecItems->Size(); i++)
  { 
     CFileItemPtr pItem = m_parent->m_vecItems->Get(i);
     StartRecordFile(pItem.get());
  }
}
//TBIRD 1.3.5 MODS - FINISH

bool CGUIWindowPVRSearch::OnContextButtonStopRecord(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_STOP_RECORD)
  {
    bReturn = true;

    StopRecordFile(item);
  }

  return bReturn;
}

//TBIRD-STAN 1.3.5 MODS - START
//TBIRD 1.3.4 MODS - START
/*bool CGUIWindowPVRSearch::OnContextButtonUser1(CFileItem *item, CONTEXT_BUTTON button)
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

bool CGUIWindowPVRSearch::OnContextButtonUser2(CFileItem *item, CONTEXT_BUTTON button)
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

bool CGUIWindowPVRSearch::OnContextButtonUser3(CFileItem *item, CONTEXT_BUTTON button)
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

bool CGUIWindowPVRSearch::OnContextButtonUser4(CFileItem *item, CONTEXT_BUTTON button)
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

bool CGUIWindowPVRSearch::OnContextButtonUser5(CFileItem *item, CONTEXT_BUTTON button)
{
  bool bReturn = false;

  if (button == CONTEXT_BUTTON_USER5)
  {
  	
      	CGUIMessage msg2(GUI_MSG_CLICKED, 36, 10601, 0);
      	g_windowManager.SendMessage(msg2); 
	bReturn = true;
  }

  return bReturn;
}*/
//TBIRD 1.3.4 MODS - FINISH
//TBIRD-STAN 1.3.5 MODS - FINISH


bool CGUIWindowPVRSearch::ActionShowSearch(CFileItem *item)
{
  if (item->GetPath() == "pvr://guide/searchresults/empty.epg")
    ShowSearchResults();
  else
    ShowEPGInfo(item);

  return true;
}

void CGUIWindowPVRSearch::ShowSearchResults()
{
  /* Load timer settings dialog */
  CGUIDialogPVRGuideSearch* pDlgInfo = (CGUIDialogPVRGuideSearch*)g_windowManager.GetWindow(WINDOW_DIALOG_PVR_GUIDE_SEARCH);

  if (!pDlgInfo)
    return;

  if (!m_bSearchStarted)
  {
    m_bSearchStarted = true;
    m_searchfilter.Reset();
  }

  pDlgInfo->SetFilterData(&m_searchfilter);

  /* Open dialog window */
  pDlgInfo->DoModal();

  //(+) Dulanga - Cancel button still goes to search issue - Start
  if (!pDlgInfo->IsConfirmed()) 
    return; 
  //(+) Dulanga - Cancel button still goes to search issue - End
    
  m_bSearchConfirmed = true;
  UpdateData();
  
  //TBIRD-STAN 1.3.5 MODS - START
  CGUIMessage msgFocus2(GUI_MSG_SETFOCUS,WINDOW_PVR,CONTROL_BTNSEARCH);
  g_windowManager.SendMessage(msgFocus2);

  CGUIMessage msgFocus1(GUI_MSG_SETFOCUS,WINDOW_PVR,CONTROL_LIST_SEARCH);
  g_windowManager.SendMessage(msgFocus1);
  //TBIRD-STAN 1.3.5 MODS - FINISH
}
