/*
	CVSNT Generic API
    Copyright (C) 2005 Tony Hoyle and March-Hare Software Ltd

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
// Db2
// Win32 specific - could be backported in theory

#ifdef _WIN32
// Microsoft braindamage reversal.  
#define _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_SECURE_NO_DEPRECATE
#define _SCL_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#define snprintf _snprintf

#include <stdlib.h>

#include <config.h>
#include <cvsapi.h>

#include <string>

#include <sql.h>
#include <sqlext.h>

#include "Db2Connection.h"
#include "Db2Recordset.h"

CDb2Field::CDb2Field()
{
	data = NULL;
}

CDb2Field::~CDb2Field()
{
	if(data)
		free(data);
}

CDb2Field::operator int()
{
	switch(ctype)
	{
		case SQL_C_WCHAR:
			{ int ret=0; swscanf((const wchar_t *)data,L"%d",&ret); return ret; }
		case SQL_C_LONG:
			return (int)*(long*)data;
		case SQL_C_DOUBLE:
			return (int)*(double*)data;
		default:
			CServerIo::trace(1,"Bogus value return for field %s",name.c_str());
			return 0;
	}
}

CDb2Field::operator long()
{
	switch(ctype)
	{
		case SQL_C_WCHAR:
			{ long ret=0; swscanf((const wchar_t *)data,L"%ld",&ret); return ret; }
		case SQL_C_LONG:
			return (long)*(long*)data;
		case SQL_C_DOUBLE:
			return (long)*(double*)data;
		default:
			CServerIo::trace(1,"Bogus value return for field %s",name.c_str());
			return 0;
	}
}

CDb2Field::operator unsigned()
{
	switch(ctype)
	{
		case SQL_C_WCHAR:
			{ unsigned ret=0; swscanf((const wchar_t *)data,L"%u",&ret); return ret; }
		case SQL_C_LONG:
			return (unsigned)*(long*)data;
		case SQL_C_DOUBLE:
			return (unsigned)*(double*)data;
		default:
			CServerIo::trace(1,"Bogus value return for field %s",name.c_str());
			return 0;
	}
}

CDb2Field::operator unsigned long()
{
	switch(ctype)
	{
		case SQL_C_WCHAR:
			{ unsigned long ret=0; swscanf((const wchar_t *)data,L"%lu",&ret); return ret; }
		case SQL_C_LONG:
			return (unsigned long)*(long*)data;
		case SQL_C_DOUBLE:
			return (unsigned long)*(double*)data;
		default:
			CServerIo::trace(1,"Bogus value return for field %s",name.c_str());
			return 0;
	}
}

CDb2Field::operator __int64()
{
	switch(ctype)
	{
		case SQL_C_WCHAR:
			{ __int64 ret=0; swscanf((const wchar_t *)data,L"%I64d",&ret); return ret; }
		case SQL_C_LONG:
			return (__int64)*(long*)data;
		case SQL_C_DOUBLE:
			return (__int64)*(double*)data;
		default:
			CServerIo::trace(1,"Bogus value return for field %s",name.c_str());
			return 0;
	}
}

CDb2Field::operator const char *()
{
	switch(ctype)
	{
		case SQL_C_WCHAR:
			tmpstr = cvs::narrow((const wchar_t*)data);
			return tmpstr.c_str();
		case SQL_C_LONG:
			cvs::sprintf(tmpstr,32,"%ld",*(long*)data);
			return tmpstr.c_str();
		case SQL_C_DOUBLE:
			cvs::sprintf(tmpstr,32,"%lf",*(double*)data);
			return tmpstr.c_str();
		default:
			CServerIo::trace(1,"Bogus value return for field %s",name.c_str());
			return 0;
	}
}

CDb2Field::operator const wchar_t *()
{
	switch(ctype)
	{
		case SQL_C_WCHAR:
			return (const wchar_t*)data;
		case SQL_C_LONG:
			cvs::swprintf(tmpwstr,32,L"%ld",*(long*)data);
			return tmpwstr.c_str();
		case SQL_C_DOUBLE:
			cvs::swprintf(tmpwstr,32,L"%lf",*(double*)data);
			return tmpwstr.c_str();
		default:
			CServerIo::trace(1,"Bogus value return for field %s",name.c_str());
			return 0;
	}
}

/**********************************************************************/

CDb2Recordset::CDb2Recordset()
{
	m_hStmt = NULL;
	m_bEof = true;
}

CDb2Recordset::~CDb2Recordset()
{
	Close();
}

bool CDb2Recordset::Init(CDb2Connection *parent, HSTMT hStmt, const char *command)
{
	m_bEof = false;
	m_parent = parent;
	m_hStmt = hStmt;

	if(!SQL_SUCCEEDED(m_parent->m_lasterror=SQLExecDirect(m_hStmt,(SQLWCHAR*)(const wchar_t *)cvs::wide(command),SQL_NTS)))
	{
		GetStmtError();
		return false;
	}

	if(!SQL_SUCCEEDED(m_parent->m_lasterror = SQLNumResultCols(m_hStmt,&m_num_fields)))
	{
		GetStmtError();
		return false;
	}

	m_sqlfields.resize(m_num_fields);
	for(SQLSMALLINT n=0; n<m_num_fields; n++)
	{
		SQLRETURN rc;

		SQLWCHAR szCol[128];
		SQLSMALLINT len = sizeof(szCol);
		rc = m_parent->m_lasterror = SQLDescribeCol(hStmt,n+1,szCol,sizeof(szCol),&len,&m_sqlfields[n].type,&m_sqlfields[n].size,&m_sqlfields[n].decimal,&m_sqlfields[n].null);
		if(!SQL_SUCCEEDED(rc))
		{
			GetStmtError();
			return false;
		}
		szCol[len]='\0';
		m_sqlfields[n].field = n;
		m_sqlfields[n].hStmt = m_hStmt;
		m_sqlfields[n].name = szCol;

		SQLINTEGER fldlen = 0;
		SQLSMALLINT ctype;
		switch(m_sqlfields[n].type)
		{
		case SQL_UNKNOWN_TYPE:
			CServerIo::trace(1,"Unable to bind column %s as it is SQL_UNKNOWN_TYPE",(const char *)szCol);
			break; // Don't bind
		case SQL_CHAR:
		case SQL_VARCHAR:
			ctype = SQL_C_WCHAR;
			fldlen = m_sqlfields[n].size;
			break;
		case SQL_DECIMAL:
			ctype = SQL_C_WCHAR;
			fldlen = m_sqlfields[n].size + m_sqlfields[n].decimal + 1;
			break;
		case SQL_NUMERIC:
		case SQL_INTEGER:
		case SQL_SMALLINT:
			ctype = SQL_C_LONG;
			fldlen = sizeof(long);
			break;
		case SQL_FLOAT:
		case SQL_REAL:
		case SQL_DOUBLE:
			ctype = SQL_C_DOUBLE;
			fldlen = sizeof(double);
			break;
		case SQL_DATETIME:
			ctype = SQL_C_WCHAR;
			fldlen = 64;
			break;
		}
		m_sqlfields[n].ctype = ctype;
		m_sqlfields[n].fldlen = fldlen;
		if(m_sqlfields[n].fldlen)
		{
			m_sqlfields[n].data = malloc(m_sqlfields[n].fldlen);
			if(!SQL_SUCCEEDED(m_parent->m_lasterror = SQLBindCol(m_hStmt,n+1,m_sqlfields[n].ctype,m_sqlfields[n].data,m_sqlfields[n].fldlen,&m_sqlfields[n].datalen)))
			{
				GetStmtError();
				CServerIo::trace(1,"Unable to bind column %s due to error",(const char*)szCol);
				return false;
			}
		}
	}

	if(m_num_fields)
	{
		if(!Next() && !m_bEof)
			return false;
	}

	return true;
}

bool CDb2Recordset::Close()
{
	if(m_hStmt)
		SQLFreeStmt(m_hStmt,SQL_DROP);
	m_hStmt = NULL;
	m_bEof = true;
	return true;
}

bool CDb2Recordset::Closed() const
{
	if(!m_hStmt)
		return false;
	return true;
}

bool CDb2Recordset::Eof() const
{
	return m_bEof;
}

bool CDb2Recordset::Next()
{
	SQLRETURN res;

	if(m_bEof)
		return false;
	m_parent->m_lasterror = res = SQLFetch(m_hStmt);
	if(res == SQL_NO_DATA_FOUND)
	{
		m_bEof = true;
		return false;
	}

	if(!SQL_SUCCEEDED(res))
	{
		GetStmtError();
		return false;
	}

	return true;
}

CSqlField* CDb2Recordset::operator[](size_t item) const
{
	if(item>=(size_t)m_num_fields)
		return NULL;
	return (CSqlField*)&m_sqlfields[item];
}

CSqlField* CDb2Recordset::operator[](int item) const
{
	if(item<0 || item>=m_num_fields)
		return NULL;
	return (CSqlField*)&m_sqlfields[item];
}

CSqlField* CDb2Recordset::operator[](const char *item) const
{
	cvs::wide _item(item);
	for(size_t n=0; n<(size_t)m_num_fields; n++)
		if(!wcsicmp(m_sqlfields[n].name.c_str(),_item))
			return (CSqlField*)&m_sqlfields[n];
	CServerIo::error("Database error - field '%s' not found in recordset.",item);
	return NULL;
}

void CDb2Recordset::GetStmtError()
{
	SQLWCHAR state[6];
	SQLINTEGER error;
	SQLSMALLINT size = 512,len;
	m_parent->m_lastrsError.resize(size);
	SQLWCHAR *pmsg = (SQLWCHAR*)m_parent->m_lastrsError.data();

	if(m_hStmt)
	{
		for(int i=1; SQL_SUCCEEDED(SQLGetDiagRec(SQL_HANDLE_STMT, m_hStmt, i, state, &error, pmsg, size, &len)); i++)
		{
			size-=len;
			pmsg+=len;
		}
	}
	m_parent->m_lastrsError.resize(512-size);
}
