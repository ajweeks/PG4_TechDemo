#include "stdafx.hpp"

#include "Helpers.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

#pragma warning(push, 0)
#include <glm/gtx/matrix_decompose.hpp>

// Fucking windows headers man...
#include "AL/al.h"
#include "CommCtrl.h"

#include "ShObjIdl.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"
#pragma warning(pop)

#include "FlexEngine.hpp"
#include "Graphics/Renderer.hpp"


namespace flex
{
	ImVec4 g_WarningTextColor(1.0f, 0.25f, 0.25f, 1.0f);
	ImVec4 g_WarningButtonColor(0.65f, 0.12f, 0.09f, 1.0f);
	ImVec4 g_WarningButtonHoveredColor(0.45f, 0.04f, 0.01f, 1.0f);
	ImVec4 g_WarningButtonActiveColor(0.35f, 0.0f, 0.0f, 1.0f);

	GLFWimage LoadGLFWimage(const std::string& filePath, i32 requestedChannelCount, bool flipVertically, i32* channelCountOut /* = nullptr */)
	{
		assert(requestedChannelCount == 3 ||
			   requestedChannelCount == 4);

		GLFWimage result = {};

		std::string fileName = filePath;
		StripLeadingDirectories(fileName);
		Print("Loading texture %s\n", fileName.c_str());

		stbi_set_flip_vertically_on_load(flipVertically);

		i32 channels;
		unsigned char* data = stbi_load(filePath.c_str(),
										&result.width,
										&result.height,
										&channels,
										(requestedChannelCount == 4  ? STBI_rgb_alpha : STBI_rgb));

		if (channelCountOut)
		{
			*channelCountOut = channels;
		}

		if (data == 0)
		{
			const char* failureReasonStr = stbi_failure_reason();
			PrintError("Couldn't load image, failure reason: %s, filepath: %s\n", failureReasonStr, filePath.c_str());
			return result;
		}
		else
		{
			assert(result.width <= Renderer::MAX_TEXTURE_DIM);
			assert(result.height <= Renderer::MAX_TEXTURE_DIM);

			result.pixels = static_cast<unsigned char*>(data);
		}

		return result;
	}

	void DestroyGLFWimage(GLFWimage& image)
	{
		stbi_image_free(image.pixels);
		image.pixels = nullptr;
	}

	bool HDRImage::Load(const std::string& hdrFilePath, i32 requestedChannelCount, bool flipVertically)
	{
		assert(requestedChannelCount == 3 ||
			   requestedChannelCount == 4);

		filePath = hdrFilePath;

		std::string fileName = hdrFilePath;
		StripLeadingDirectories(fileName);
		Print("Loading HDR texture %s\n", fileName.c_str());

		stbi_set_flip_vertically_on_load(flipVertically);

		pixels = stbi_loadf(filePath.c_str(),
							&width,
							&height,
							&channelCount,
							(requestedChannelCount == 4 ? STBI_rgb_alpha : STBI_rgb));

		channelCount = 4;

		if (!pixels)
		{
			PrintError("Failed to load HDR image at %s\n", filePath.c_str());
			return false;
		}

		assert(width <= Renderer::MAX_TEXTURE_DIM);
		assert(height <= Renderer::MAX_TEXTURE_DIM);
		assert(channelCount > 0);

		return true;
	}

	void HDRImage::Free()
	{
		stbi_image_free(pixels);
	}

	std::string FloatToString(real f, i32 precision)
	{
		std::stringstream stream;

		stream << std::fixed << std::setprecision(precision) << f;

		return stream.str();
	}

	std::string IntToString(i32 i, u16 minChars)
	{
		std::string result = std::to_string(glm::abs(i));

		if (i < 0)
		{
			if (result.length() < minChars)
			{
				result = '-' + std::string(minChars - result.length(), '0') + result;
			}
			else
			{
				result = '-' + result;
			}
		}
		else
		{
			if (result.length() < minChars)
			{
				result = std::string(minChars - result.length(), '0') + result;
			}
		}

		return result;
	}

	TextCache::TextCache(const std::string& str, AnchorPoint anchor, glm::vec2 pos, glm::vec4 color, real xSpacing, bool bRaw, const std::vector<glm::vec2>& letterOffsets) :
		str(str),
		anchor(anchor),
		pos(pos),
		color(color),
		xSpacing(xSpacing),
		bRaw(bRaw),
		letterOffsets(letterOffsets)
	{
	}

	bool FileExists(const std::string& filePath)
	{
		std::ifstream file(filePath.c_str());
		bool exists = file.good();
		file.close();

		return exists;
	}

	bool ReadFile(const std::string& filePath, std::string& fileContents, bool bBinaryFile)
	{
		int fileMode = std::ios::in | std::ios::ate;
		if (bBinaryFile)
		{
			fileMode |= std::ios::binary;
		}
		std::ifstream file(filePath.c_str(), fileMode);

		if (!file)
		{
			PrintError("Unable to read file: %s\n", filePath.c_str());
			return false;
		}

		std::streampos length = file.tellg();

		fileContents.resize((size_t)length);

		file.seekg(0, std::ios::beg);
		file.read(&fileContents[0], length);
		file.close();

		// Remove extra null terminators caused by Windows line endings
		for (u32 charIndex = 0; charIndex < fileContents.size() - 1; ++charIndex)
		{
			if (fileContents[charIndex] == '\0')
			{
				fileContents = fileContents.substr(0, charIndex);
			}
		}

		return true;
	}

	bool ReadFile(const std::string& filePath, std::vector<char>& vec, bool bBinaryFile)
	{
		i32 fileMode = std::ios::in | std::ios::ate;
		if (bBinaryFile)
		{
			fileMode |= std::ios::binary;
		}
		std::ifstream file(filePath.c_str(), fileMode);

		if (!file)
		{
			PrintError("Unable to read file: %s\n", filePath.c_str());
			return false;
		}

		std::streampos length = file.tellg();

		vec.resize((size_t)length);

		file.seekg(0, std::ios::beg);
		file.read(vec.data(), length);
		file.close();

		return true;
	}

	bool WriteFile(const std::string& filePath, const std::string& fileContents, bool bBinaryFile)
	{
		std::vector<char> vec(fileContents.begin(), fileContents.end());
		return WriteFile(filePath, vec, bBinaryFile);
	}

	bool WriteFile(const std::string& filePath, const std::vector<char>& vec, bool bBinaryFile)
	{
		i32 fileMode =std::ios::out | std::ios::trunc;
		if (bBinaryFile)
		{
			fileMode |= std::ios::binary;
		}
		std::ofstream fileStream(filePath, fileMode);

		if (fileStream.is_open())
		{
			fileStream.write(&vec[0], vec.size());
			fileStream.close();

			return true;
		}

		return false;
	}

	bool DeleteFile(const std::string& filePath, bool bPrintErrorOnFailure)
	{
		if (::DeleteFile(filePath.c_str()))
		{
			return true;
		}
		else
		{
			if (bPrintErrorOnFailure)
			{
				PrintError("Failed to delete file %s\n", filePath.c_str());
			}
			return false;
		}
	}

	bool CopyFile(const std::string& filePathFrom, const std::string& filePathTo)
	{
		if (::CopyFile(filePathFrom.c_str(), filePathTo.c_str(), 0))
		{
			return true;
		}
		else
		{
			PrintError("Failed to copy file from \"%s\" to \"%s\"\n", filePathFrom.c_str(), filePathTo.c_str());
			return false;
		}
	}

	bool DirectoryExists(const std::string& absoluteDirectoryPath)
	{
		if (absoluteDirectoryPath.find("..") != std::string::npos)
		{
			PrintError("Attempted to create directory using relative path! Must specify absolute path!\n");
			return false;
		}

		DWORD dwAttrib = GetFileAttributes(absoluteDirectoryPath.c_str());

		return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
			    dwAttrib & FILE_ATTRIBUTE_DIRECTORY);
	}

	void OpenExplorer(const std::string& absoluteDirectory)
	{
		ShellExecute(NULL, "open", absoluteDirectory.c_str(), NULL, NULL, SW_SHOWDEFAULT);

		// OR
		// system("explorer C:\\");
	}

	bool OpenJSONFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath)
	{
		char filter[] = "JSON files\0*.json\0\0";
		return OpenFileDialog(windowTitle, absoluteDirectory, outSelectedAbsFilePath, filter);
	}

	bool OpenFileDialog(const std::string& windowTitle, const std::string& absoluteDirectory, std::string& outSelectedAbsFilePath, char filter[] /* = nullptr */)
	{
		//TaskDialog(NULL, NULL, L"Open somethin", L"D:\\", NULL, TDCBF_OK_BUTTON, TD_INFORMATION_ICON, NULL);
		//return false;

		OPENFILENAME ofna = {};
		ofna.lStructSize = sizeof(OPENFILENAME);
		ofna.lpstrInitialDir = absoluteDirectory.c_str();
		ofna.nMaxFile = ARRAY_SIZE(filter);
		if (ofna.nMaxFile && filter)
		{
			ofna.lpstrFilter = filter;
		}
		ofna.nFilterIndex = 0;
		const i32 MAX_FILE_PATH_LEN = 512;
		char fileBuf[MAX_FILE_PATH_LEN];
		memset(fileBuf, '\0', MAX_FILE_PATH_LEN);
		ofna.lpstrFile = fileBuf;
		ofna.nMaxFile = MAX_FILE_PATH_LEN;
		ofna.lpstrTitle = windowTitle.c_str();
		ofna.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
		bool bSuccess = GetOpenFileName(&ofna) == 1;

		if (ofna.lpstrFile)
		{
			//ofna.nFileOffset;
			//ofna.nFileExtension;
			outSelectedAbsFilePath = ofna.lpstrFile;
		}

		return bSuccess;
	}

	bool FindFilesInDirectory(const std::string& directoryPath, std::vector<std::string>& filePaths, const std::string& fileType)
	{
		std::string cleanedFileType = fileType;
		{
			size_t dotPos = cleanedFileType.find('.');
			if (dotPos != std::string::npos)
			{
				cleanedFileType.erase(dotPos, 1);
			}
		}

		bool bPathContainsBackslash = (directoryPath.find('\\') != std::string::npos);
		char slashChar = (bPathContainsBackslash ? '\\' : '/');

		std::string cleanedDirPath = directoryPath;
		if (cleanedDirPath[cleanedDirPath.size() - 1] != slashChar)
		{
			cleanedDirPath += slashChar;
		}

		std::string cleanedDirPathWithWildCard = cleanedDirPath + '*';


		HANDLE hFind;
		WIN32_FIND_DATAA findData;

		hFind = FindFirstFile(cleanedDirPathWithWildCard.c_str(), &findData);

		if (hFind == INVALID_HANDLE_VALUE)
		{
			PrintError("Failed to find any file in directory %s\n", cleanedDirPath.c_str());
			return false;
		}

		do
		{
			if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				// Skip over directories
				//Print(findData.cFileName);
			}
			else
			{
				bool foundFileTypeMatches = false;
				if (cleanedFileType == "*")
				{
					foundFileTypeMatches = true;
				}
				else
				{
					std::string fileNameStr(findData.cFileName);
					size_t dotPos = fileNameStr.find('.');

					if (dotPos != std::string::npos)
					{
						std::string foundFileType = Split(fileNameStr, '.')[1];
						if (foundFileType == cleanedFileType)
						{
							foundFileTypeMatches = true;
						}
					}
				}

				if (foundFileTypeMatches)
				{
					// File size retrieval:
					//LARGE_INTEGER filesize;
					//filesize.LowPart = findData.nFileSizeLow;
					//filesize.HighPart = findData.nFileSizeHigh;

					filePaths.push_back(cleanedDirPath + findData.cFileName);
				}
			}
		} while (FindNextFile(hFind, &findData) != 0);

		DWORD dwError = GetLastError();
		if (dwError != ERROR_NO_MORE_FILES)
		{
			PrintError("Error encountered while finding files in directory %s\n", cleanedDirPath.c_str());
			return false;
		}

		return true;
	}

	void StripLeadingDirectories(std::string& filePath)
	{
		size_t finalSlash = filePath.rfind('/');
		if (finalSlash == std::string::npos)
		{
			finalSlash = filePath.rfind('\\');
		}

		if (finalSlash == std::string::npos)
		{
			return; // There are no directories to remove
		}
		else
		{
			filePath = filePath.substr(finalSlash + 1);
		}
	}

	void ExtractDirectoryString(std::string& filePath)
	{
		size_t finalSlash = filePath.rfind('/');
		if (finalSlash == std::string::npos)
		{
			finalSlash = filePath.rfind('\\');
		}

		if (finalSlash == std::string::npos)
		{
			return; // There are no directories to remove
		}
		else
		{
			filePath = filePath.substr(0, finalSlash + 1);
		}
	}

	void StripFileType(std::string& filePath)
	{
		if (filePath.find('.') != std::string::npos)
		{
			filePath = Split(filePath, '.')[0];
		}
	}

	void ExtractFileType(std::string& filePathInTypeOut)
	{
		if (filePathInTypeOut.find('.') != std::string::npos)
		{
			filePathInTypeOut = Split(filePathInTypeOut, '.')[1];
		}
	}

	void CreateDirectoryRecursive(const std::string& absoluteDirectoryPath)
	{
		if (absoluteDirectoryPath.find("..") != std::string::npos)
		{
			PrintError("Attempted to create directory using relative path! Must specify absolute path!\n");
			return;
		}

		if (DirectoryExists(absoluteDirectoryPath))
		{
			// Directory already exists!
			return;
		}

		u32 pos = 0;
		do
		{
			pos = absoluteDirectoryPath.find_first_of("\\/", pos + 1);
			CreateDirectory(absoluteDirectoryPath.substr(0, pos).c_str(), NULL);
			GetLastError();// == ERROR_ALREADY_EXISTS;
		} while (pos != std::string::npos);
	}

	bool ParseWAVFile(const std::string& filePath, i32* format, u8** data, i32* size, i32* freq)
	{
		std::vector<char> dataArray;
		if (!ReadFile(filePath, dataArray, true))
		{
			PrintError("Failed to parse WAV file: %s\n", filePath.c_str());
			return false;
		}

		if (dataArray.size() < 12)
		{
			PrintError("Invalid WAV file: %s\n", filePath.c_str());
			return false;
		}

		u32 dataIndex = 0;
		std::string chunkID(&dataArray[dataIndex], 4); dataIndex += 4;

		u32 chunkSize = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		std::string waveID(&dataArray[dataIndex], 4); dataIndex += 4;
		std::string subChunk1ID(&dataArray[dataIndex], 4); dataIndex += 4;

		if (chunkID.compare("RIFF") != 0 ||
			waveID.compare("WAVE") != 0 ||
			subChunk1ID.compare("fmt ") != 0)
		{
			PrintError("Invalid WAVE file header: %s\n", filePath.c_str());
			return false;
		}

		// TODO: Unsigned?
		u32 subChunk1Size = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		if (subChunk1Size != 16)
		{
			PrintError("Non-16 bit chunk size in WAVE files in unsupported: %s\n", filePath.c_str());
			return false;
		}

		u16 audioFormat = Parse16u(&dataArray[dataIndex]); dataIndex += 2;
		if (audioFormat != 1) // WAVE_FORMAT_PCM
		{
			PrintError("WAVE file uses unsupported format (only PCM is allowed): %s\n", filePath.c_str());
			return false;
		}

		u16 channelCount = Parse16u(&dataArray[dataIndex]); dataIndex += 2;
		u32 samplesPerSec = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		u32 avgBytesPerSec = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		u16 blockAlign = Parse16u(&dataArray[dataIndex]); dataIndex += 2;
		u16 bitsPerSample = Parse16u(&dataArray[dataIndex]); dataIndex += 2;

		std::string subChunk2ID(&dataArray[dataIndex], 4); dataIndex += 4;
		if (subChunk2ID.compare("data") != 0)
		{
			PrintError("Invalid WAVE file: %s\n", filePath.c_str());
			return false;
		}

		u32 subChunk2Size = Parse32u(&dataArray[dataIndex]); dataIndex += 4;
		*data = new u8[subChunk2Size];
		for (u32 i = 0; i < subChunk2Size; ++i)
		{
			(*data)[i] = dataArray[dataIndex];
			++dataIndex;
		}

		bool bPrintWavStats = false;
		if (bPrintWavStats)
		{
			std::string fileName = filePath;
			StripLeadingDirectories(fileName);
			Print("Stats about WAV file: %s:\n\tchannel count: %i, samples/s: %i, average bytes/s: %i"
				  ", block align: %i, bits/sample: %i, chunk size: %i, sub chunk2 ID: \"%s\", sub chunk 2 size: %i\n",
				  fileName.c_str(),
				  channelCount,
				  samplesPerSec,
				  avgBytesPerSec,
				  blockAlign,
				  bitsPerSample,
				  chunkSize,
				  subChunk2ID.c_str(),
				  subChunk2Size);
		}

		switch (channelCount)
		{
		case 1:
		{
			switch (bitsPerSample)
			{
			case 8:
				*format = AL_FORMAT_MONO8;
				break;
			case 16:
				*format = AL_FORMAT_MONO16;
				break;
			default:
				PrintError("WAVE file contains invalid bitsPerSample (must be 8 or 16): %i\n", bitsPerSample);
				break;
			}
		} break;
		case 2:
		{
			switch (bitsPerSample)
			{
			case 8:
				*format = AL_FORMAT_STEREO8;
				break;
			case 16:
				*format = AL_FORMAT_STEREO16;
				break;
			default:
				PrintError("WAVE file contains invalid bitsPerSample (must be 8 or 16): %i\n", bitsPerSample);
				break;
			}
		} break;
		default:
		{
			PrintError("WAVE file contains invalid channel count (must be 1 or 2): %i\n", channelCount);
		} break;
		}

		*size = subChunk2Size;
		*freq = samplesPerSec;

		return true;
	}

	u32 Parse32u(char* ptr)
	{
		return ((u8)ptr[0]) + ((u8)ptr[1] << 8) + ((u8)ptr[2] << 16) + ((u8)ptr[3] << 24);
	}

	u16 Parse16u(char* ptr)
	{
		return ((u8)ptr[0]) + ((u8)ptr[1] << 8);
	}

	std::string GetDateString_YMD()
	{
		std::stringstream result;

		SYSTEMTIME time;
		GetLocalTime(&time);

		result << time.wYear << '-' << std::setw(2) << std::setfill('0') << time.wMonth << '-' << time.wDay;

		return result.str();
	}

	std::string GetDateString_YMDHMS()
	{
		std::stringstream result;

		SYSTEMTIME time;
		GetLocalTime(&time);

		result << time.wYear << '-' << std::setw(2) << std::setfill('0') << time.wMonth << '-' << time.wDay <<
			'_' << time.wHour << '-' << time.wMinute << '-' << time.wSecond;

		return result.str();
	}

	std::vector<std::string> Split(const std::string& str, char delim)
	{
		std::vector<std::string> result;
		size_t i = 0;

		size_t strLen = str.size();
		while (i != strLen)
		{
			while (i != strLen && str[i] == delim)
			{
				++i;
			}

			size_t j = i;
			while (j != strLen && str[j] != delim)
			{
				++j;
			}

			if (i != j)
			{
				result.push_back(str.substr(i, j - i));
				i = j;
			}
		}

		return result;
	}

	i32 NextNonAlphaNumeric(const std::string& str, i32 offset)
	{
		while (offset < (i32)str.size())
		{
			if (!isdigit(str[offset]) && !isalpha(str[offset]))
			{
				return offset;
			}
			++offset;
		}

		return -1;
	}

	bool NearlyEquals(real a, real b, real threshhold)
	{
		return (abs(a - b) < threshhold);
	}

	bool NearlyEquals(const glm::vec2& a, const glm::vec2& b, real threshhold)
	{
		return (abs(a.x - b.x) < threshhold) &&
			(abs(a.y - b.y) < threshhold);
	}

	bool NearlyEquals(const glm::vec3& a, const glm::vec3& b, real threshhold)
	{
		return (abs(a.x - b.x) < threshhold) &&
			(abs(a.y - b.y) < threshhold) &&
			(abs(a.z - b.z) < threshhold);
	}

	bool NearlyEquals(const glm::vec4& a, const glm::vec4& b, real threshhold)
	{
		return (abs(a.x - b.x) < threshhold) &&
			(abs(a.y - b.y) < threshhold) &&
			(abs(a.z - b.z) < threshhold) &&
			(abs(a.w - b.w) < threshhold);
	}

	real Lerp(real a, real b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	glm::vec2 Lerp(const glm::vec2 & a, const glm::vec2 & b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	glm::vec4 Lerp(const glm::vec4& a, const glm::vec4& b, real t)
	{
		return a * (1.0f - t) + b * t;
	}

	real ParseFloat(const std::string& floatStr)
	{
		if (floatStr.empty())
		{
			PrintError("Invalid float string (empty)\n");
			return -1.0f;
		}

		return (real)std::atof(floatStr.c_str());
	}

	glm::vec2 ParseVec2(const std::string& vecStr)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if (parts.size() != 2)
		{
			PrintError("Invalid vec2 field: %s\n", vecStr.c_str());
			return glm::vec2(-1);
		}
		else
		{
			glm::vec2 result(
				std::atof(parts[0].c_str()),
				std::atof(parts[1].c_str()));

			return result;
		}
	}

	glm::vec3 ParseVec3(const std::string& vecStr)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if (parts.size() != 3 && parts.size() != 4)
		{
			PrintError("Invalid vec3 field: %s\n", vecStr.c_str());
			return glm::vec3(-1);
		}
		else
		{
			glm::vec3 result(
				std::atof(parts[0].c_str()),
				std::atof(parts[1].c_str()),
				std::atof(parts[2].c_str()));

			return result;
		}
	}

	glm::vec4 ParseVec4(const std::string& vecStr, real defaultW)
	{
		std::vector<std::string> parts = Split(vecStr, ',');

		if ((parts.size() != 4 && parts.size() != 3) || (defaultW < 0 && parts.size() != 4))
		{
			PrintError("Invalid vec4 field: %s\n", vecStr.c_str());
			return glm::vec4(-1);
		}
		else
		{
			glm::vec4 result;

			if (parts.size() == 4)
			{
				result = glm::vec4(
					std::atof(parts[0].c_str()),
					std::atof(parts[1].c_str()),
					std::atof(parts[2].c_str()),
					std::atof(parts[3].c_str()));
			}
			else
			{
				result = glm::vec4(
					std::atof(parts[0].c_str()),
					std::atof(parts[1].c_str()),
					std::atof(parts[2].c_str()),
					defaultW);
			}

			return result;
		}
	}

	bool IsNanOrInf(real val)
	{
		return isnan(val) || isinf(val);
	}

	bool IsNanOrInf(const glm::vec2& vec)
	{
		return (isnan(vec.x) || isnan(vec.y) ||
				isinf(vec.x) || isinf(vec.y));
	}

	bool IsNanOrInf(const glm::vec3& vec)
	{
		return (isnan(vec.x) || isnan(vec.y) || isnan(vec.z) ||
				isinf(vec.x) || isinf(vec.y) || isinf(vec.z));
	}

	bool IsNanOrInf(const glm::vec4& vec)
	{
		return (isnan(vec.x) || isnan(vec.y) || isnan(vec.z) || isnan(vec.w) ||
				isinf(vec.x) || isinf(vec.y) || isinf(vec.z) || isinf(vec.w));
	}

	bool IsNanOrInf(const glm::quat& quat)
	{
		return (isnan(quat.x) || isnan(quat.y) || isnan(quat.z) || isnan(quat.w) ||
				isinf(quat.x) || isinf(quat.y) || isinf(quat.z) || isinf(quat.w));
	}

	std::string GetIncrementedPostFixedStr(const std::string& namePrefix, const std::string& defaultName)
	{
		if (namePrefix.empty())
		{
			return defaultName;
		}

		std::string name = namePrefix;

		i16 numChars;
		i32 numEndingWith = GetNumberEndingWith(namePrefix, numChars);

		if (numEndingWith == -1)
		{
			return defaultName;
		}
		else
		{
			std::string result = namePrefix.substr(0, namePrefix.size() - numChars) + IntToString(numEndingWith + 1, numChars);
			return result;
		}
	}

	void PadEnd(std::string& str, i32 minLen, char pad)
	{
		if ((i32)str.length() >= minLen)
		{
			return;
		}

		str = str + std::string(minLen - str.length(), pad);
	}

	void PadStart(std::string& str, i32 minLen, char pad)
	{
		if ((i32)str.length() >= minLen)
		{
			return;
		}

		str = std::string(minLen - str.length(), pad) + str;
	}

	std::string Vec2ToString(const glm::vec2& vec, i32 precision)
	{
		std::string result(FloatToString(vec.x, precision) + ", " +
			FloatToString(vec.y, precision));
		return result;
	}

	std::string Vec3ToString(const glm::vec3& vec, i32 precision)
	{
		std::string result(FloatToString(vec.x, precision) + ", " +
			FloatToString(vec.y, precision) + ", " +
			FloatToString(vec.z, precision));
		return result;
	}

	std::string Vec4ToString(const glm::vec4& vec, i32 precision)
	{
		std::string result(FloatToString(vec.x, precision) + ", " +
			FloatToString(vec.y, precision) + ", " +
			FloatToString(vec.z, precision) + ", " +
			FloatToString(vec.w, precision));
		return result;
	}

	void CopyVec3ToClipboard(const glm::vec3& vec)
	{
		CopyColorToClipboard(glm::vec4(vec, 1.0f));
	}

	void CopyVec4ToClipboard(const glm::vec4& vec)
	{
		// TODO: Don't use ImGui for clipboard management since we might want to disable ImGui but still use the clipboard
		ImGui::LogToClipboard();

		ImGui::LogText("%.2ff,%.2ff,%.2ff,%.2ff", vec.x, vec.y, vec.z, vec.w);

		ImGui::LogFinish();
	}

	void CopyColorToClipboard(const glm::vec3& col)
	{
		CopyVec4ToClipboard(glm::vec4(col, 1.0f));
	}

	void CopyColorToClipboard(const glm::vec4& col)
	{
		CopyVec4ToClipboard(col);
	}

	void CopyTransformToClipboard(Transform* transform)
	{
		ImGui::LogToClipboard();

		glm::vec3 pos = transform->GetWorldPosition();
		glm::vec3 rot = glm::eulerAngles(transform->GetWorldRotation());
		glm::vec3 scale = transform->GetWorldScale();
		ImGui::LogText("%.2ff,%.2ff,%.2ff,%.2ff,%.2ff,%.2ff,%.2ff,%.2ff,%.2ff",
					   pos.x, pos.y, pos.z, rot.x, rot.y, rot.z, scale.x, scale.y, scale.z);

		ImGui::LogFinish();
	}

	bool PasteTransformFromClipboard(Transform* transform)
	{
		std::string clipboardText = ImGui::GetClipboardText();

		if (clipboardText.empty())
		{
			PrintError("Attempted to paste transform from empty clipboard!\n");
			return false;
		}

		std::vector<std::string> clipboardParts = Split(clipboardText, ',');
		if (clipboardParts.size() != 9)
		{
			PrintError("Attempted to paste transform from clipboard but it doesn't contain a valid transform object! Contents: %s\n", clipboardText.c_str());
			return false;
		}

		transform->SetWorldPosition(glm::vec3(stof(clipboardParts[0]), stof(clipboardParts[1]), stof(clipboardParts[2])), false);
		transform->SetWorldRotation(glm::vec3(stof(clipboardParts[3]), stof(clipboardParts[4]), stof(clipboardParts[5])), false);
		transform->SetWorldScale(glm::vec3(stof(clipboardParts[6]), stof(clipboardParts[7]), stof(clipboardParts[8])), true);

		return true;
	}

	glm::vec3 PasteColor3FromClipboard()
	{
		glm::vec4 color4 = PasteColor4FromClipboard();

		return glm::vec3(color4);
	}

	glm::vec4 PasteColor4FromClipboard()
	{
		const std::string clipboardContents = ImGui::GetClipboardText();

		const size_t comma1 = clipboardContents.find(',');
		const size_t comma2 = clipboardContents.find(',', comma1 + 1);
		const size_t comma3 = clipboardContents.find(',', comma2 + 1);

		if (comma1 == std::string::npos ||
			comma2 == std::string::npos ||
			comma3 == std::string::npos)
		{
			// Clipboard doesn't contain correctly formatted color!
			return VEC4_ZERO;
		}

		glm::vec4 result(
			stof(clipboardContents.substr(0, comma1)),
			stof(clipboardContents.substr(comma1 + 1, comma2 - comma1 - 1)),
			stof(clipboardContents.substr(comma2 + 1, comma3 - comma2 - 1)),
			stof(clipboardContents.substr(comma3 + 1 ))
		);

		return result;
	}

	CullFace StringToCullFace(const std::string& str)
	{
		std::string strLower(str);
		ToLower(strLower);

		if (strLower.compare("back") == 0)
		{
			return CullFace::BACK;
		}
		else if (strLower.compare("front") == 0)
		{
			return CullFace::FRONT;
		}
		else if (strLower.compare("front and back") == 0)
		{
			return CullFace::FRONT_AND_BACK;
		}
		else
		{
			PrintError("Unhandled cull face str: %s\n", str.c_str());
			return CullFace::NONE;
		}
	}

	std::string CullFaceToString(CullFace cullFace)
	{
		switch (cullFace)
		{
		case CullFace::BACK:			return "back";
		case CullFace::FRONT:			return "front";
		case CullFace::FRONT_AND_BACK:	return "front and back";
		case CullFace::NONE:			return "NONE";
		default:						return "UNHANDLED CULL FACE";
		}
	}

	void ToLower(std::string& str)
	{
		for (char& c : str)
		{
			c = (char)tolower(c);
		}
	}

	void ToUpper(std::string& str)
	{
		for (char& c : str)
		{
			c = (char)toupper(c);
		}
	}

	bool StartsWith(const std::string& str, const std::string& start)
	{
		if (str.length() < start.length())
		{
			return false;
		}

		bool result = (str.substr(0, start.length()).compare(start) == 0);
		return result;
	}

	bool EndsWith(const std::string& str, const std::string& end)
	{
		if (str.length() < end.length())
		{
			return false;
		}

		bool result = (str.substr(str.length() - end.length()).compare(end) == 0);
		return result;
	}

	i32 GetNumberEndingWith(const std::string& str, i16& outNumNumericalChars)
	{
		if (str.empty())
		{
			outNumNumericalChars = 0;
			return -1;
		}

		u16 strLen = (u16)str.size();

		if (!isdigit(str[strLen - 1]))
		{
			outNumNumericalChars = 0;
			return -1;
		}

		i16 firstDigit = strLen - 1;
		while (firstDigit >= 0 && isdigit(str[firstDigit]))
		{
			firstDigit--;
		}
		firstDigit++;

		i32 num = (i32)atoi(str.substr(firstDigit).c_str());
		outNumNumericalChars = (strLen - firstDigit);

		return num;
	}

	const char* GameObjectTypeToString(GameObjectType type)
	{
		return GameObjectTypeStrings[(i32)type];
	}

	GameObjectType StringToGameObjectType(const char* gameObjectTypeStr)
	{
		for (i32 i = 0; i < (i32)GameObjectType::NONE; ++i)
		{
			if (strcmp(GameObjectTypeStrings[i], gameObjectTypeStr) == 0)
			{
				return (GameObjectType)i;
			}
		}

		return GameObjectType::NONE;
	}

	void RetrieveCurrentWorkingDirectory()
	{
		char cwdBuffer[MAX_PATH];
		char* ans = _getcwd(cwdBuffer, sizeof(cwdBuffer));
		if (ans)
		{
			FlexEngine::s_CurrentWorkingDirectory = ans;
		}
	}

	std::string RelativePathToAbsolute(const std::string& relativePath)
	{
		size_t nextDoubleDot = relativePath.find("..");

		std::string workingDirectory = FlexEngine::s_CurrentWorkingDirectory;

		std::string strippedFilePath = relativePath;

		while (nextDoubleDot != std::string::npos)
		{
			size_t lastSlash = workingDirectory.find_last_of("\\/");
			if (lastSlash == std::string::npos)
			{
				PrintWarn("Invalidly formed relative path! %s\n", relativePath.c_str());
				nextDoubleDot = std::string::npos;
			}
			else
			{
				workingDirectory = workingDirectory.substr(0, lastSlash);
				strippedFilePath = strippedFilePath.substr(nextDoubleDot);
				nextDoubleDot = relativePath.find("..", nextDoubleDot + 2);
			}
		}

		std::for_each(strippedFilePath.begin(), strippedFilePath.end(), [](char& c)
		{
			if (c == '/')
			{
				c = '\\';
			}
		});

		std::string absolutePath = workingDirectory + '\\' + strippedFilePath;

		return absolutePath;
	}

	bool DoImGuiRotationDragFloat3(const char* label, glm::vec3& rotation, glm::vec3& outCleanedRotation)
	{
		glm::vec3 pRot = rotation;

		bool bValueChanged = ImGui::DragFloat3(label, &rotation[0], 0.1f);
		if (ImGui::IsItemClicked(1))
		{
			rotation = VEC3_ZERO;
			bValueChanged = true;
		}

		outCleanedRotation = rotation;

		if ((rotation.y >= 90.0f && pRot.y < 90.0f) ||
			(rotation.y <= -90.0f && pRot.y > 90.0f))
		{
			outCleanedRotation.y = 180.0f - rotation.y;
			rotation.x += 180.0f;
			rotation.z += 180.0f;
		}

		if (rotation.y > 90.0f)
		{
			// Prevents "pop back" when dragging past the 90 deg mark
			outCleanedRotation.y = 180.0f - rotation.y;
		}

		outCleanedRotation.x = rotation.x;
		outCleanedRotation.z = rotation.z;

		return bValueChanged;
	}

	bool SaveImage(const std::string& absoluteFilePath, ImageFormat imageFormat, i32 width, i32 height, i32 channelCount, u8* data)
	{
		if (data == nullptr ||
			width == 0 ||
			height == 0 ||
			channelCount == 0 ||
			absoluteFilePath.empty())
		{
			PrintError("Attempted to save invalid image to %s\n", absoluteFilePath.c_str());
			return false;
		}

		bool bResult = false;

		const char* fileNameCstr = absoluteFilePath.c_str();
		switch (imageFormat)
		{
		case ImageFormat::JPG:
		{
			const i32 JPEGQuality = 90;
			if (stbi_write_jpg(fileNameCstr, width, height, channelCount, data, JPEGQuality))
			{
				bResult = true;
			}
		} break;
		case ImageFormat::TGA:
		{
			if (stbi_write_tga(fileNameCstr, width, height, channelCount, data))
			{
				bResult = true;
			}
		} break;
		case ImageFormat::PNG:
		{
			i32 strideInBytes = sizeof(data[0]) * channelCount * width;
			if (stbi_write_png(fileNameCstr, width, height, 4, data, strideInBytes))
			{
				bResult = true;
			}
		} break;
		case ImageFormat::BMP:
		{
			if (stbi_write_bmp(fileNameCstr, width, height, channelCount, data))
			{
				bResult = true;
			}
		} break;
		}

		return bResult;
	}
} // namespace flex
