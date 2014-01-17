/*************************************************************************
Copyright (c) 2012-2013 Miroslav Andel
All rights reserved.

For conditions of distribution and use, see copyright notice in sgct.h
*************************************************************************/

#include "../include/sgct/SGCTSettings.h"
#include "../include/sgct/MessageHandler.h"
#include "../include/sgct/ScreenCapture.h"
#include "../include/sgct/ogl_headers.h"

#define DEFAULT_NUMBER_OF_CAPTURE_THREADS 8

sgct::SGCTSettings * sgct::SGCTSettings::mInstance = NULL;

sgct::SGCTSettings::SGCTSettings()
{
	mPNGCompressionLevel = 1;
	mNumberOfCaptureThreads = DEFAULT_NUMBER_OF_CAPTURE_THREADS;

	mUseDepthTexture = false;
	mUseNormalTexture = false;
	mUsePositionTexture = false;
	mUseFBO = true;

	mSwapInterval = 1;
	mRefreshRate = 0;
	mOSDTextOffset[0] = 0.05f;
	mOSDTextOffset[1] = 0.05f;

	//FXAA parameters
	mFXAASubPixTrim = 1.0f/4.0f;
	mFXAASubPixOffset = 1.0f/2.0f;

	for(size_t i=0; i<3; i++)
		mCapturePath[i].assign("SGCT");
	mCaptureFormat = sgct_core::ScreenCapture::NOT_SET;

	mCurrentDrawBuffer = Diffuse;
	mCurrentBufferFloatPrecision = Float_16Bit;

	//font stuff
	mFontSize = 10;
	#if __WIN32__
    mFontName = "verdanab.ttf";
    #elif __APPLE__
    mFontName = "Tahoma Bold.ttf";
    #else
    mFontName = "FreeSansBold.ttf";
    #endif
}

sgct::SGCTSettings::~SGCTSettings()
{
	;
}

/*!
Set swap interval for all windows
	- -1 = adaptive sync (Nvidia)
	- 0  = vertical sync off
	- 1  = wait for vertical sync
	- 2  = fix when using swapgroups in xp and running half the framerate
*/
void sgct::SGCTSettings::setSwapInterval(int val)
{
	mSwapInterval = val;
}

/*!
Get swap interval for all windows
	- -1 = adaptive sync (Nvidia)
	- 0  = vertical sync off
	- 1  = wait for vertical sync
	- 2  = fix when using swapgroups in xp and running half the framerate
*/
int sgct::SGCTSettings::getSwapInterval()
{
	return mSwapInterval;
}

/*!
	Set the refreshrate hint of the window in fullscreen mode.
	If it's not listed in your monitor's video-mode list than it will not be used.

	\param freq the refresh frequency/rate
*/
void sgct::SGCTSettings::setRefreshRateHint(int freq)
{
	mRefreshRate = freq;
}

/*!
	Get the refreshrate hint of the window in fullscreen mode.
*/
int sgct::SGCTSettings::getRefreshRateHint()
{
	return mRefreshRate;
}


/*!
Set to true if depth buffer textures should be allocated and used.
*/
void sgct::SGCTSettings::setUseDepthTexture(bool state)
{
	mUseDepthTexture = state;
}

/*!
Set to true if normal textures should be allocated and used.
*/
void sgct::SGCTSettings::setUseNormalTexture(bool state)
{
	mUseNormalTexture = state;
	updateDrawBufferFlag();
}

/*!
Set to true if position buffer textures should be allocated and used.
*/
void sgct::SGCTSettings::setUsePositionTexture(bool state)
{
	mUsePositionTexture = state;
	updateDrawBufferFlag();
}

/*!
Set the float precision of the float buffers (normal and position buffer)
@param bfp is the float precition that will be used in next buffer resize or creation
*/
void sgct::SGCTSettings::setBufferFloatPrecision(BufferFloatPrecision bfp)
{
	mCurrentBufferFloatPrecision = bfp;
}

/*!
Update the draw buffer flags
*/
void sgct::SGCTSettings::updateDrawBufferFlag()
{
	if (mUseNormalTexture && mUsePositionTexture)
		mCurrentDrawBuffer = Diffuse_Normal_Position;
	else if (!mUseNormalTexture && !mUsePositionTexture)
		mCurrentDrawBuffer = Diffuse;
	else if (mUseNormalTexture && !mUsePositionTexture)
		mCurrentDrawBuffer = Diffuse_Normal;
	else
		mCurrentDrawBuffer = Diffuse_Position;
}

/*!
Set the FBO mode. This is done internally using SGCT config file.
*/
void sgct::SGCTSettings::setUseFBO(bool state)
{
	mUseFBO = state;
}

/*!
Set the number of capture threads used by SGCT (multi-threaded screenshots)
*/
void sgct::SGCTSettings::setNumberOfCaptureThreads(int count)
{
	mNumberOfCaptureThreads = count;
}

/*!
Set the zlib compression level used for saving png files

Compression levels 1-9.
	-1 = Default compression\n
	0 = No compression\n
	1 = Best speed\n
	9 = Best compression\n
*/
void sgct::SGCTSettings::setPNGCompressionLevel(int level)
{
	mPNGCompressionLevel = level;
}

/*!
Set capture/screenshot path used by SGCT

\param path the path including filename without suffix
\param cpi index to which path to set (Mono = default, Left or Right)
*/
void sgct::SGCTSettings::setCapturePath(std::string path, sgct::SGCTSettings::CapturePathIndexes cpi)
{
	if( path.empty() ) //invalid filename
	{
		sgct::MessageHandler::instance()->print(sgct::MessageHandler::NOTIFY_ERROR, "SGCTSettings: Empty screen capture path!\n");
		return;
	}

	mCapturePath[cpi].assign(path);
}

/*!
Append capture/screenshot path used by SGCT

\param str the string to append including filename without suffix
\param cpi index to which path to set (Mono = default, Left or Right)
*/
void sgct::SGCTSettings::appendCapturePath(std::string str, sgct::SGCTSettings::CapturePathIndexes cpi)
{
	mCapturePath[cpi].append( str );
}

/*!
Set the capture format which can be one of the following:
-PNG
-TGA
*/
void sgct::SGCTSettings::setCaptureFormat(const char * format)
{
	if( strcmp("png", format) == 0 || strcmp("PNG", format) == 0 )
	{
		mCaptureFormat = sgct_core::ScreenCapture::PNG;
	}
	else if( strcmp("tga", format) == 0 || strcmp("TGA", format) == 0 )
	{
		mCaptureFormat = sgct_core::ScreenCapture::TGA;
	}
}

/*!
	Get the capture/screenshot path

	\param cpi index to which path to get (Mono = default, Left or Right)
*/
const char * sgct::SGCTSettings::getCapturePath(sgct::SGCTSettings::CapturePathIndexes cpi)
{
	return mCapturePath[cpi].c_str();
}

/*!
	Get the capture/screenshot path

	\return the captureformat if set, otherwise -1 is returned
*/
int sgct::SGCTSettings::getCaptureFormat()
{
	return mCaptureFormat;
}

/*!
	Controls removal of sub-pixel aliasing.
	- 1/2 - low removal
	- 1/3 - medium removal
	- 1/4 - default removal
	- 1/8 - high removal
	- 0 - complete removal
*/
void sgct::SGCTSettings::setFXAASubPixTrim(float val)
{
	mFXAASubPixTrim = val;
}

/*!
	Set the pixel offset for contrast/edge detection. Values should be in the range [1.0f/8.0f, 1.0f]. Default is 0.5f.
*/
void sgct::SGCTSettings::setFXAASubPixOffset(float val)
{
	mFXAASubPixOffset = val;
}


/*!
	Set the horizontal OSD text Offset between 0.0 and 1.0
*/
void sgct::SGCTSettings::setOSDTextXOffset(float val)
{
	mOSDTextOffset[ 0 ] = val;
}

/*!
	Set the vertical OSD text Offset between 0.0 and 1.0
*/
void sgct::SGCTSettings::setOSDTextYOffset(float val)
{
	mOSDTextOffset[ 1 ] = val;
}

/*!
	Set the OSD text font size
*/
void sgct::SGCTSettings::setOSDTextFontSize( int size )
{
	mFontSize = size;
}

/*!
	Set the OSD text font name
*/
void sgct::SGCTSettings::setOSDTextFontName( std::string name )
{
	mFontName.assign( name );
}

/*!
	Set the OSD text font path
*/
void sgct::SGCTSettings::setOSDTextFontPath( std::string path )
{
	mFontPath.assign( path );
}

/*!
	Get the OSD text font size
*/
const int &	sgct::SGCTSettings::getOSDTextFontSize()
{
	return mFontSize;
}

/*!
	Get the OSD text font name
*/
const std::string & sgct::SGCTSettings::getOSDTextFontName()
{
	return mFontName;
}

/*!
	Get the OSD text font path
*/
const std::string & sgct::SGCTSettings::getOSDTextFontPath()
{
	return mFontPath;
}

/*!
	Get the precision of the float buffers as an GLint (GL_RGB16F or GL_RGB32F)
*/
int	sgct::SGCTSettings::getBufferFloatPrecisionAsGLint()
{
	return mCurrentBufferFloatPrecision == Float_16Bit ? GL_RGB16F : GL_RGB32F;
}
