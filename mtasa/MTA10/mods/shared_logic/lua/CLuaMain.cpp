/*****************************************************************************
*
*  PROJECT:     Multi Theft Auto v1.0
*               (Shared logic for modifications)
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        mods/shared_logic/lua/CLuaMain.cpp
*  PURPOSE:     Lua main
*  DEVELOPERS:  Kevin Whiteside <kevuwk@gmail.com>
*               Cecill Etheredge <ijsf@gmx.net>
*               Derek Abdine <>
*               Jax <>
*               Ed Lyons <eai@opencoding.net>
*
*****************************************************************************/

#include "StdInc.h"
#define DECLARE_PROFILER_SECTION_CLuaMain
#include "profiler/SharedUtil.Profiler.h"

using std::list;

extern CClientGame* g_pClientGame;

static CLuaManager* m_pLuaManager;

#define HOOK_INSTRUCTION_COUNT 1000000
#define HOOK_MAXIMUM_TIME 5000


// This script is loaded into all VM's created.
const char szPreloadedScript [] = ""\

    // Code for allowing this syntax:   exports.resourceName:exportedFunctionName (...)
    //                                  exports["resourceName"]:exportedFunctionName (...)
    //                                  exports[resourcePointer]:exportedFunctionName (...)
    // Aswell as the old:               call ( getResourceFromName ( "resourceName" ), "exportedFunctionName", ... )
    "local rescallMT = {}\n" \
    "function rescallMT:__index(k)\n" \
    "        if type(k) ~= 'string' then k = tostring(k) end\n" \
    "        self[k] = function(resExportTable, ...)\n" \
    "                if type(self.res) == 'userdata' and getResourceRootElement(self.res) then\n" \
    "                        return call(self.res, k, ...)\n" \
    "                else\n" \
    "                        return nil\n" \
    "                end\n" \
    "        end\n" \
    "        return self[k]\n" \
    "end\n" \
    "local exportsMT = {}\n" \
    "function exportsMT:__index(k)\n" \
    "        if type(k) == 'userdata' and getResourceRootElement(k) then\n" \
    "                return setmetatable({ res = k }, rescallMT)\n" \
    "        elseif type(k) ~= 'string' then\n" \
    "                k = tostring(k)\n" \
    "        end\n" \
    "        local res = getResourceFromName(k)\n" \
    "        if res then\n" \
    "                return setmetatable({ res = res }, rescallMT)\n" \
    "        else\n" \
    "                outputDebugString('exports: Call to non-running client resource (' .. k .. ')', 1)\n" \
    "                return setmetatable({}, rescallMT)\n" \
    "        end\n" \
    "end\n" \
    "exports = setmetatable({}, exportsMT)\n";


CLuaMain::CLuaMain ( CLuaManager* pLuaManager, CResource* pResourceOwner )
{
    // Initialise everything to be setup in the Start function
    m_pLuaManager = pLuaManager;
    m_luaVM = NULL;
    m_bBeingDeleted = false;
    m_pLuaTimerManager = new CLuaTimerManager;
    m_FunctionEnterTimer.SetMaxIncrement ( 500 );
    
    m_pResource = pResourceOwner;

    CClientPerfStatLuaMemory::GetSingleton ()->OnLuaMainCreate ( this );
    CClientPerfStatLuaTiming::GetSingleton ()->OnLuaMainCreate ( this );
}


CLuaMain::~CLuaMain ( void )
{
    g_pClientGame->GetLatentTransferManager ()->OnLuaMainDestroy ( this );
    g_pClientGame->GetScriptDebugging()->OnLuaMainDestroy ( this );

    // Unload the current script
    UnloadScript ();
    m_bBeingDeleted = true;

    // Delete the timer manager
    delete m_pLuaTimerManager;

    CClientPerfStatLuaMemory::GetSingleton ()->OnLuaMainDestroy ( this );
    CClientPerfStatLuaTiming::GetSingleton ()->OnLuaMainDestroy ( this );
}

bool CLuaMain::BeingDeleted ( void )
{
    return m_bBeingDeleted;
}


void CLuaMain::ResetInstructionCount ( void )
{
    m_FunctionEnterTimer.Reset ();
}


void CLuaMain::InitSecurity ( void )
{
    lua_register ( m_luaVM, "dofile", CLuaFunctionDefs::DisabledFunction );
    lua_register ( m_luaVM, "loadfile", CLuaFunctionDefs::DisabledFunction );
    lua_register ( m_luaVM, "require", CLuaFunctionDefs::DisabledFunction );
    lua_register ( m_luaVM, "loadlib", CLuaFunctionDefs::DisabledFunction );
    lua_register ( m_luaVM, "getfenv", CLuaFunctionDefs::DisabledFunction );
    lua_register ( m_luaVM, "newproxy", CLuaFunctionDefs::DisabledFunction );
}


void CLuaMain::InitVM ( void )
{
    assert( !m_luaVM );

    // Create a new VM
    m_luaVM = lua_open ();
    m_pLuaManager->OnLuaMainOpenVM( this, m_luaVM );


    // Set the instruction count hook
    lua_sethook ( m_luaVM, InstructionCountHook, LUA_MASKCOUNT, HOOK_INSTRUCTION_COUNT );

    // Load LUA libraries
    luaopen_base ( m_luaVM );
    luaopen_math ( m_luaVM );
    luaopen_string ( m_luaVM );
    luaopen_table ( m_luaVM );
    luaopen_debug ( m_luaVM );

    // Initialize security restrictions. Very important to prevent lua trojans and viruses!
    InitSecurity();

    // Register module functions
    CLuaCFunctions::RegisterFunctionsWithVM ( m_luaVM );

    // Update global variables
    lua_pushelement ( m_luaVM, g_pClientGame->GetRootEntity () );
    lua_setglobal ( m_luaVM, "root" );

    lua_pushresource ( m_luaVM, m_pResource );
    lua_setglobal ( m_luaVM, "resource" );

    lua_pushelement ( m_luaVM, m_pResource->GetResourceEntity () );
    lua_setglobal ( m_luaVM, "resourceRoot" );

    lua_pushelement ( m_luaVM, m_pResource->GetResourceGUIEntity () );
    lua_setglobal ( m_luaVM, "guiRoot" );

    lua_pushelement ( m_luaVM, g_pClientGame->GetLocalPlayer() );
    lua_setglobal ( m_luaVM, "localPlayer" );

    // Load pre-loaded lua code
    LoadScript ( szPreloadedScript );
}


void CLuaMain::InstructionCountHook ( lua_State* luaVM, lua_Debug* pDebug )
{
    // Grab our lua VM
    CLuaMain* pLuaMain = m_pLuaManager->GetVirtualMachine ( luaVM );
    if ( pLuaMain )
    {
        // Above max time?
        if ( pLuaMain->m_FunctionEnterTimer.Get () > HOOK_MAXIMUM_TIME )
        {
            // Print it in the console
            CLogger::ErrorPrintf ( "Infinite/too long execution (%s)", pLuaMain->GetScriptName () );
            
            SString strAbortInf = "Aborting; infinite running script in ";
            strAbortInf += pLuaMain->GetScriptName ();
            
            // Error out
            lua_pushstring ( luaVM, strAbortInf );
            lua_error ( luaVM );
        }
    }
}


bool CLuaMain::LoadScriptFromBuffer ( const char* cpInBuffer, unsigned int uiInSize, const char* szFileName )
{
    SString strNiceFilename = ConformResourcePath( szFileName );

    // Decrypt if required
    const char* cpBuffer;
    uint uiSize;
    if ( !g_pNet->DecryptScript( cpInBuffer, uiInSize, &cpBuffer, &uiSize, strNiceFilename ) )
    {
        // Problems problems
#if MTA_DM_VERSION < 0x135 
        SString strMessage( "%s is invalid and will not work in future versions. Please re-compile at http://luac.mtasa.com/", *strNiceFilename ); 
        g_pClientGame->GetScriptDebugging()->LogWarning ( m_luaVM, "Script warning: %s", *strMessage );
        g_pClientGame->TellServerSomethingImportant( 1003, SStringX( "CLIENT SCRIPT ERROR: " ) + strMessage, true );
        // cpBuffer is always valid after call to DecryptScript
#else
        SString strMessage( "%s is invalid. Please re-compile at http://luac.mtasa.com/", *strNiceFilename ); 
        g_pClientGame->GetScriptDebugging()->LogError ( m_luaVM, "Loading script failed: %s", *strMessage );
        g_pClientGame->TellServerSomethingImportant( 1003, SStringX( "CLIENT SCRIPT ERROR: " ) + strMessage, true );
        return false;
#endif
    }

    bool bUTF8;

    // UTF-8 BOM?  Compare by checking the standard UTF-8 BOM
    if ( IsUTF8BOM( cpBuffer, uiSize ) == false )
    {
        // Maybe not UTF-8, if we have a >80% heuristic detection confidence, assume it is
        bUTF8 = ( GetUTF8Confidence ( (const unsigned char*)cpBuffer, uiSize ) >= 80 );
    }
    else
    {
        // If there's a BOM, load ignoring the first 3 bytes
        bUTF8 = true;
        cpBuffer += 3;
        uiSize -= 3;
    }

    // If compiled script, make sure correct chunkname is embedded
    if ( strNiceFilename != "(unknown)" )
        EmbedChunkName( strNiceFilename, &cpBuffer, &uiSize );

    if ( m_luaVM )
    {
        // Are we not marked as UTF-8 already, and not precompiled?
        std::string strUTFScript;
        if ( !bUTF8 && ( uiSize < 5 || cpBuffer[0] != 27 || cpBuffer[1] != 'L' || cpBuffer[2] != 'u' || cpBuffer[3] != 'a' || cpBuffer[4] != 'Q' ) )
        {
            std::string strBuffer = std::string(cpBuffer, uiSize);
            strUTFScript = UTF16ToMbUTF8(ANSIToUTF16( strBuffer ));
            if ( uiSize != strUTFScript.size() )
            {
                uiSize = strUTFScript.size();
                g_pClientGame->GetScriptDebugging()->LogWarning ( m_luaVM, "Script '%s' is not encoded in UTF-8.  Loading as ANSI...", strNiceFilename.c_str() );
            }
        }
        else
            strUTFScript = std::string(cpBuffer, uiSize);

        // Run the script
        if ( luaL_loadbuffer ( m_luaVM, bUTF8 ? cpBuffer : strUTFScript.c_str(), uiSize, SString ( "@%s", *strNiceFilename ) ) )
        {
            // Print the error
            std::string strRes = lua_tostring( m_luaVM, -1 );
            if ( strRes.length () )
            {
                CLogger::LogPrintf ( "SCRIPT ERROR: %s\n", strRes.c_str () );
                g_pClientGame->GetScriptDebugging()->LogError ( m_luaVM, "Loading script failed: %s", strRes.c_str () );
            }
            else
            {
                CLogger::LogPrint ( "SCRIPT ERROR: Unknown\n" );
                g_pClientGame->GetScriptDebugging()->LogError ( m_luaVM, "Loading script failed for unknown reason" );
            }
        }
        else
        {
            ResetInstructionCount ();
            int luaSavedTop = lua_gettop ( m_luaVM );
            int iret = this->PCall ( m_luaVM, 0, LUA_MULTRET, 0 ) ;
            if ( iret == LUA_ERRRUN || iret == LUA_ERRMEM )
            {
                SString strRes = lua_tostring( m_luaVM, -1 );
        
                std::vector <SString> vecSplit;
                strRes.Split ( ":", vecSplit );
                
                if ( vecSplit.size ( ) >= 3 )
                {
                    SString strFile = vecSplit[0];
                    int     iLine   = atoi ( vecSplit[1].c_str ( ) );
                    SString strMsg  = vecSplit[2].substr ( 1 );
                    
                    g_pClientGame->GetScriptDebugging()->LogError ( strFile, iLine, strMsg );
                }
                else
                {
                    if ( !strRes.ContainsI ( ExtractFilename ( strNiceFilename ) ) )
                    {
                        // Add filename to error message, if not already present
                        strRes = SString ( "%s (global scope) - %s", *strNiceFilename, *strRes );
                    }
                    g_pClientGame->GetScriptDebugging()->LogError ( m_luaVM, "%s", strRes.c_str () );
                }
            }
            // Cleanup any return values
            if ( lua_gettop ( m_luaVM ) > luaSavedTop )
                lua_settop( m_luaVM, luaSavedTop );
            return true;
        }
    }

    return false;
}


bool CLuaMain::LoadScript ( const char* szLUAScript )
{
    if ( m_luaVM )
    {
        // Run the script
        if ( !luaL_loadbuffer ( m_luaVM, szLUAScript, strlen(szLUAScript), NULL ) )
        {
            ResetInstructionCount ();
            int luaSavedTop = lua_gettop ( m_luaVM );
            int iret = this->PCall ( m_luaVM, 0, LUA_MULTRET, 0 ) ;
            if ( iret == LUA_ERRRUN || iret == LUA_ERRMEM )
            {
                std::string strRes = ConformResourcePath ( lua_tostring( m_luaVM, -1 ) );
                g_pClientGame->GetScriptDebugging()->LogError ( m_luaVM, "Executing in-line script failed: %s", strRes.c_str () );
            }
            // Cleanup any return values
            if ( lua_gettop ( m_luaVM ) > luaSavedTop )
                lua_settop( m_luaVM, luaSavedTop );
        }
        else
        {
            std::string strRes = ConformResourcePath ( lua_tostring( m_luaVM, -1 ) );
            g_pClientGame->GetScriptDebugging()->LogError ( m_luaVM, "Loading in-line script failed: %s", strRes.c_str () );
        }
    }
    else
        return false;

    return true;
}


void CLuaMain::Start ( void )
{

}


void CLuaMain::UnloadScript ( void )
{
    // ACHTUNG: UNLOAD MODULES!

    // Delete all timers and events
    m_pLuaTimerManager->RemoveAllTimers ();

    // Delete all GUI elements
    //m_pLuaManager->m_pGUIManager->DeleteAll ( this );

/*
// done at server version:
    // Delete all keybinds
    list < CPlayer* > ::const_iterator iter = m_pPlayerManager->IterBegin ();
    for ( ; iter != m_pPlayerManager->IterEnd (); iter++ )
    {
        if ( (*iter)->IsJoined () )
            (*iter)->GetKeyBinds ()->RemoveAllKeys ( this );
    }
*/
    // End the lua vm
    if ( m_luaVM )
    {
        CLuaFunctionRef::RemoveLuaFunctionRefsForVM( m_luaVM );
        m_pLuaManager->OnLuaMainCloseVM( this, m_luaVM );
        m_pLuaManager->AddToPendingDeleteList ( m_luaVM );
        m_luaVM = NULL;
    }
}


void CLuaMain::DoPulse ( void )
{
    m_pLuaTimerManager->DoPulse ( this );
}


CXMLFile * CLuaMain::CreateXML ( const char * szFilename )
{
    CXMLFile * pFile = g_pCore->GetXML ()->CreateXML ( szFilename, true );
    if ( pFile )
        m_XMLFiles.push_back ( pFile );
    return pFile;
}

void CLuaMain::DestroyXML ( CXMLFile * pFile )
{
    if ( !m_XMLFiles.empty() ) m_XMLFiles.remove ( pFile );
    delete pFile;
}

void CLuaMain::DestroyXML ( CXMLNode * pRootNode )
{
    list<CXMLFile *>::iterator iter;
    for ( iter = m_XMLFiles.begin(); iter != m_XMLFiles.end(); iter++ )
    {
        CXMLFile * file = (*iter);
        if ( file )
        {
            if ( file->GetRootNode() == pRootNode )
            {
                delete file;
                m_XMLFiles.erase ( iter );
                break;
            }
        }
    }
}

void CLuaMain::SaveXML ( CXMLNode * pRootNode )
{
    list<CXMLFile *>::iterator iter;
    for ( iter = m_XMLFiles.begin(); iter != m_XMLFiles.end(); iter++ )
    {
        CXMLFile * file = (*iter);
        if ( file )
        {
            if ( file->GetRootNode() == pRootNode )
            {
                file->Write();
                break;
            }
        }
    }
    if ( m_pResource )
    {
        list < CResourceConfigItem* > ::iterator iter = m_pResource->ConfigIterBegin ();
        for ( ; iter != m_pResource->ConfigIterEnd () ; iter++ )
        {
            CResourceConfigItem* pConfigItem = *iter;
            if ( pConfigItem->GetRoot () == pRootNode )
            {
                CXMLFile* pFile = pConfigItem->GetFile ();
                if ( pFile )
                {
                    pFile->Write ();
                }
                break;
            }
        }
    }
}


///////////////////////////////////////////////////////////////
//
// CLuaMain::GetElementCount
//
//
//
///////////////////////////////////////////////////////////////
unsigned long CLuaMain::GetElementCount ( void ) const
{
    if ( m_pResource && m_pResource->GetElementGroup () ) 
        return m_pResource->GetElementGroup ()->GetCount ();
    return 0;
}


///////////////////////////////////////////////////////////////
//
// CLuaMain::GetFunctionTag
//
// Turn iFunctionNumber into something human readable
//
///////////////////////////////////////////////////////////////
const SString& CLuaMain::GetFunctionTag ( int iLuaFunction )
{
    // Find existing
    SString* pTag = MapFind ( m_FunctionTagMap, iLuaFunction );
#ifndef MTA_DEBUG
    if ( !pTag )
#endif
    {
        // Create if required
        SString strText;

        lua_Debug debugInfo;
        lua_getref ( m_luaVM, iLuaFunction );
        if ( lua_getinfo( m_luaVM, ">nlS", &debugInfo ) )
        {
            // Make sure this function isn't defined in a string
            if ( debugInfo.source[0] == '@' )
            {
                //std::string strFilename2 = ConformResourcePath ( debugInfo.source );
                SString strFilename = debugInfo.source;

                int iPos = strFilename.find_last_of ( "/\\" );
                if ( iPos >= 0 )
                    strFilename = strFilename.substr ( iPos + 1 );

                strText = SString ( "@%s:%d", strFilename.c_str (), debugInfo.currentline != -1 ? debugInfo.currentline : debugInfo.linedefined, iLuaFunction );
            }
            else
            {
                strText = SString ( "@func_%d %s", iLuaFunction, debugInfo.short_src );
            }
        }
        else
        {
            strText = SString ( "@func_%d NULL", iLuaFunction );
        }

    #ifdef MTA_DEBUG
        if ( pTag )
        {
            // Check tag remains unchanged
            assert ( strText == *pTag );
            return *pTag;
        }
    #endif

        MapSet ( m_FunctionTagMap, iLuaFunction, strText );
        pTag = MapFind ( m_FunctionTagMap, iLuaFunction );
    }
    return *pTag;
}


///////////////////////////////////////////////////////////////
//
// CLuaMain::PCall
//
// lua_pcall call wrapper
//
///////////////////////////////////////////////////////////////
int CLuaMain::PCall ( lua_State *L, int nargs, int nresults, int errfunc )
{
    TIMING_CHECKPOINT( "+pcall" );
    g_pClientGame->GetScriptDebugging()->PushLuaMain ( this );
    int iret = lua_pcall ( L, nargs, nresults, errfunc );
    g_pClientGame->GetScriptDebugging()->PopLuaMain ( this );
    TIMING_CHECKPOINT( "-pcall" );
    return iret;
}
