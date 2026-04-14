#include "AppwriteClientInstance.h"

AppwriteHttpClient& getAppwriteClient()
{
    static AppwriteHttpClient instance(
        APPWRITE_ENDPOINT,
        APPWRITE_PROJECT_ID,
        APPWRITE_API_KEY,
        90000 // timeout in milliseconds
    );
    return instance;
}