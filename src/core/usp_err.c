/*
 *
 * Copyright (C) 2016-2019  ARRIS Enterprises, LLC
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 * \file usp_err.c
 *
 * Functions for setting and getting the error message
 * These functions are necessary to get meaningful error messages back to the controller
 *
 */

/*************************************************************************************
 USP Agent error handling coding style:
 1. USP_ERR_SetMessage() should be called at all places where error is first encountered
 2. All intermediate code blocks which just pass the error back up, should not call USP_ERR_SetMessage()
 3. Fatal errors should be handled by calling USP_ERR_Terminate_XXX() functions, or the macros which call them

**************************************************************************************/

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sqlite3.h>
#include <signal.h>

#include "common_defs.h"
#include "usp_log.h"
#include "os_utils.h"

//------------------------------------------------------------------------------------
// Buffer to hold error message
static char usp_error[USP_ERR_MAXLEN] = { 0 };

//--------------------------------------------------------------------
// Forward declarations. Note these are not static, because we need them in the symbol table for USP_LOG_Callstack() to show them
void SegFaultHandler(int sig);

/*********************************************************************//**
**
** USP_ERR_Init
**
** Initialises error handling
**
** \param   None
**
** \return  None
**
**************************************************************************/
void USP_ERR_Init(void)
{
    signal(SIGSEGV, SegFaultHandler);
}

/*********************************************************************//**
**
** USP_ERR_SetMessage
**
** Sets the stored USP error message to that specified
** NOTE: If this function is called from any thread which is not the data model thread, then it just logs the error message
**       rather than storing it in the USP error message buffer
**
** \param   fmt - printf style format
**
** \return  None
**
**************************************************************************/
void USP_ERR_SetMessage(char *fmt, ...)
{
    va_list ap;
    char *buf_to_use = usp_error;
    int buf_len = sizeof(usp_error);
    char local_buf[USP_ERR_MAXLEN];
    
    // Write the message into a local buffer, if this function is not being called from the data model thread
    if (OS_UTILS_IsDataModelThread(__FUNCTION__)==false)
    {
        buf_to_use = local_buf;
        buf_len = sizeof(local_buf);
    }

    // Write the USP error message into the buffer (local store or global USP error message)
    va_start(ap, fmt);
    vsnprintf(buf_to_use, buf_len, fmt, ap);
    buf_to_use[buf_len-1] = '\0';
    va_end(ap);

    // Log the message, if log level permits it
    if (usp_log_level >= kLogLevel_Error)
    {
        USP_LOG_Puts(kLogType_Debug, buf_to_use);
    }
    
    // Print the callstack, if debugging is enabled
    if (enable_callstack_debug)
    {
        USP_LOG_Callstack();
    }
}

/*********************************************************************//**
**
** USP_ERR_SetMessage_Sql
**
** Sets the stored USP error message for an SQlite error
** This is just a helper function, which prevents the proliferation of the common format string in the codebase
**
** \param   func - name of calling function in which sqlite function failure occurred
** \param   line - line number of calling file in which sqlite function failure occurred
** \param   sqlfunc - name of SQL function which failed
** \param   dbhandle - SQLite database handle
**
** \return  None
**
**************************************************************************/
void USP_ERR_SetMessage_Sql(const char *func, int line, const char *sqlfunc, void *db_handle)
{
    USP_ERR_SetMessage("%s(%d): %s failed: (err=%d) %s", func, line, sqlfunc, sqlite3_extended_errcode(db_handle), sqlite3_errmsg(db_handle));
}

/*********************************************************************//**
**
** USP_ERR_SetMessage_SqlParam
**
** Sets the stored USP error message for an SQlite error
** This is just a helper function, which prevents the proliferation of the common format string in the codebase
**
** \param   func - name of calling function in which sqlite function failure occurred
** \param   line - line number of calling file in which sqlite function failure occurred
** \param   sqlfunc - name of SQL function which failed
** \param   dbhandle - SQLite database handle
**
** \return  None
**
**************************************************************************/
void USP_ERR_SetMessage_SqlParam(const char *func, int line, const char *sqlfunc, void *db_handle)
{
    USP_ERR_SetMessage("%s(%d): %s failed (err=%d) %s", func, line, sqlfunc, sqlite3_extended_errcode(db_handle), sqlite3_errmsg(db_handle));
}

/*********************************************************************//**
**
** USP_ERR_SetMessage_Errno
**
** Sets the stored USP error message for function which sets errno
** This is just a helper function, which prevents the proliferation of the common format string in the codebase
**
** \param   func - name of calling function in which function failure occurred
** \param   line - line number of calling file in which function failure occurred
** \param   failed_func - name of function which failed
** \param   err - error code returned by the function. Usually this is just errno.
**
** \return  None
**
**************************************************************************/
void USP_ERR_SetMessage_Errno(const char *func, int line, const char *failed_func, int err)
{
    char buf[USP_ERR_MAXLEN];

    USP_ERR_SetMessage("%s(%d): %s failed : (err=%d) %s", func, line, failed_func, err, strerror_r(err, buf, sizeof(buf)) );
}

/*********************************************************************//**
**
** USP_ERR_ClearMessage
**
** Clears the stored USP error message
** This function is called before calling a vendor hook function, so that we
** can determine whether the vendor hook function has set an error message
** and if not, set a meaningful message
**
** \param   None
**
** \return  None
**
**************************************************************************/
void USP_ERR_ClearMessage(void)
{
    usp_error[0] = '\0';
}

/*********************************************************************//**
**
** USP_ERR_ReplaceEmptyMessage
**
** If the stored USP error message is empty, then replace it with the specified error message
**
** \param   fmt - printf style format
**
** \return  None
**
**************************************************************************/
void USP_ERR_ReplaceEmptyMessage(char *fmt, ...)
{
    va_list ap;

    // Exit if stored USP error messsage is not empty
    if (usp_error[0] != '\0')
    {
        return;
    }

    // Write the USP error message into the local store
    va_start(ap, fmt);
    vsnprintf(usp_error, sizeof(usp_error), fmt, ap);
    usp_error[sizeof(usp_error)-1] = '\0';
    va_end(ap);

    if (usp_log_level >= kLogLevel_Error)
    {
        USP_LOG_Puts(kLogType_Debug, usp_error);
    }
}

/*********************************************************************//**
**
** USP_ERR_GetMessage
**
** Returns a pointer to the USP error message
**
** \param   None
**
** \return  pointer to USP error message
**
**************************************************************************/
char *USP_ERR_GetMessage(void)
{
    return usp_error;
}

/*********************************************************************//**
**
** USP_ERR_Terminate
**
** Logs the specified message, then exits
**
** \param   fmt - printf style format
**
** \return  None
**
**************************************************************************/
void USP_ERR_Terminate(char *fmt, ...)
{
    va_list ap;
    
    // Log the cause of exit
    va_start(ap, fmt);
    vsnprintf(usp_error, sizeof(usp_error), fmt, ap);
    usp_error[sizeof(usp_error)-1] = '\0';
    va_end(ap);
    
    if (usp_log_level >= kLogLevel_Error)
    {
        USP_LOG_Puts(kLogType_Debug, usp_error);
        USP_LOG_Callstack();
        USP_LOG_Puts(kLogType_Debug, "Exiting USP Agent");
    }
    
    abort();    // call abort() rather than exit() so that a core dump is created
}

/*********************************************************************//**
**
** USP_ERR_TerminateBadCase
**
** Called to log the fact that a switch statement was passed an unexpected value, and so the executable should exit
** This is just a helper function, which prevents the proliferation of the common format string in the codebase
**
** \param   func - name of function in which switch failed
** \param   line - line number of file in which switch failed
** \param   value - the bad value for the case used in the switch statement
**
** \return  None
**
**************************************************************************/
void USP_ERR_Terminate_BadCase(const char *func, int line, int value)
{
    USP_ERR_Terminate("%s(%d): Unexpected case (%d) in switch", func, line, value);
}

/*********************************************************************//**
**
** USP_ERR_Terminate_OnAssert
**
** Logs the line causing the assertion failure, then exits the executable
** This is just a helper function, which prevents the proliferation of the common format string in the codebase
**
** \param   func - name of function in which assert failed
** \param   line - line number of file in which assert failed
** \param   statement - string form of assert statement that failed
**
** \return  None
**
**************************************************************************/
void USP_ERR_Terminate_OnAssert(const char *func, int line, char *statement)
{
    USP_ERR_Terminate("Failed assert at %s(%d): %s", func, line, statement);
}

/*********************************************************************//**
**
** SegFaultHandler
**
** Prints a callstack of where the fault occurred, then aborts USP Agent
**
** \param   sig - signal which caused this handler to be called (not used)
**
** \return  None
**
**************************************************************************/
void SegFaultHandler(int sig)
{
    USP_LOG_Error("ERROR: Segmentation Fault");
    USP_LOG_Callstack();
    abort();    // call abort() rather than exit() so that a core dump is created
}

