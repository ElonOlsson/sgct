/*************************************************************************
Copyright (c) 2012-2015 Miroslav Andel
All rights reserved.

For conditions of distribution and use, see copyright notice in sgct.h 
*************************************************************************/

#include "../include/sgct/BaseViewport.h"
#include "../include/sgct/ClusterManager.h"
//#include "../include/sgct/MessageHandler.h"

bool sgct_core::BaseViewport::isEnabled()
{
	return mEnabled;
}

/*!
Name this viewport
*/
void sgct_core::BaseViewport::setName(const std::string & name)
{
	mName = name;
}

void sgct_core::BaseViewport::setPos(float x, float y)
{
	mX = x;
	mY = y;
}

void sgct_core::BaseViewport::setSize(float x, float y)
{
	mXSize = x;
	mYSize = y;
}

void sgct_core::BaseViewport::setEnabled(bool state)
{
	mEnabled = state;
}

std::string sgct_core::BaseViewport::getName()
{
	return mName;
}

float sgct_core::BaseViewport::getX()
{
	return mX;
}

float sgct_core::BaseViewport::getY()
{
	return mY;
}

float sgct_core::BaseViewport::getXSize()
{
	return mXSize;
}

float sgct_core::BaseViewport::getYSize()
{
	return mYSize;
}

void sgct_core::BaseViewport::setUser(sgct_core::SGCTUser * user)
{
	mUser = user;
}

void sgct_core::BaseViewport::setUserName(std::string userName)
{
	mUserName.assign(userName);
	linkUserName();
}

void sgct_core::BaseViewport::linkUserName()
{
	SGCTUser * user = ClusterManager::instance()->getUserPtr(mUserName);
	if (user != NULL)
	{
		mUser = user;
	}
}