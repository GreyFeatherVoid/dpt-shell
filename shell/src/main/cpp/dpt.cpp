//
// Created by luoyesiqiu
//

#include "dpt.h"

using namespace dpt;
//缓存变量
static jobject g_realApplicationInstance = nullptr;
static jclass g_realApplicationClass = nullptr;
void* zip_addr = nullptr;
off_t zip_size;
char *appComponentFactoryChs = nullptr;
char *applicationNameChs = nullptr;
void *codeItemFilePtr = nullptr;

static JNINativeMethod gMethods[] = {
        {"craoc", "(Ljava/lang/String;)V",                               (void *) callRealApplicationOnCreate},
        {"craa",  "(Landroid/content/Context;Ljava/lang/String;)V",      (void *) callRealApplicationAttach},
        {"ia",    "(Landroid/content/Context;)V", (void *) init_app},
        {"gap",   "()Ljava/lang/String;",         (void *) getApkPathExport},
        {"gdp",   "()Ljava/lang/String;",         (void *) getCompressedDexesPathExport},
        {"rcf",   "()Ljava/lang/String;",         (void *) readAppComponentFactory},
        {"rapn",   "()Ljava/lang/String;",         (void *) readApplicationName},
        {"mde",   "(Ljava/lang/ClassLoader;Ljava/lang/ClassLoader;)V",        (void *) mergeDexElements},
        {"rde",   "(Ljava/lang/ClassLoader;Ljava/lang/String;)V",        (void *) removeDexElements},
        {"ra", "(Ljava/lang/String;)V",                               (void *) replaceApplication}
};

void mergeDexElements(JNIEnv* env,jclass __unused,jobject oldClassLoader,jobject newClassLoader){
    dalvik_system_BaseDexClassLoader oldBaseDexClassLoader(env,oldClassLoader);
    dalvik_system_BaseDexClassLoader newBaseDexClassLoader(env,newClassLoader);
    jobject oldDexPathListObj = oldBaseDexClassLoader.getPathList();
    jobject newDexPathListObj = newBaseDexClassLoader.getPathList();

    dalvik_system_DexPathList newDexPathList(env,newDexPathListObj);
    dalvik_system_DexPathList oldDexPathList(env,oldDexPathListObj);

    jobjectArray newClassLoaderDexElements = newDexPathList.getDexElements();

    jobjectArray oldClassLoaderDexElements = oldDexPathList.getDexElements();

    jint oldLen = env->GetArrayLength(oldClassLoaderDexElements);
    jint newLen = env->GetArrayLength(newClassLoaderDexElements);

    DLOGD("mergeDexElements oldlen = %d , newlen = %d",oldLen,newLen);

    dalvik_system_DexPathList::Element element(env,nullptr);

    jclass ElementClass = element.getClass();

    jobjectArray  newElementArray = env->NewObjectArray(oldLen + newLen,ElementClass, nullptr);

    for(int i = 0;i < newLen;i++) {
        jobject elementObj = env->GetObjectArrayElement(newClassLoaderDexElements, i);
        env->SetObjectArrayElement(newElementArray,i,elementObj);
    }

    for(int i = newLen;i < oldLen + newLen;i++) {
        jobject elementObj = env->GetObjectArrayElement(oldClassLoaderDexElements, i - newLen);
        env->SetObjectArrayElement(newElementArray,i,elementObj);
    }

    oldDexPathList.setDexElements(newElementArray);

    DLOGD("mergeDexElements success");
}

void removeDexElements(JNIEnv* env,jclass __unused,jobject classLoader,jstring elementName){
    dalvik_system_BaseDexClassLoader oldBaseDexClassLoader(env,classLoader);

    jobject dexPathListObj = oldBaseDexClassLoader.getPathList();

    dalvik_system_DexPathList dexPathList(env,dexPathListObj);

    jobjectArray dexElements = dexPathList.getDexElements();

    jint oldLen = env->GetArrayLength(dexElements);

    jint newLen = oldLen;
    const char *removeElementNameChs = env->GetStringUTFChars(elementName,nullptr);

    //推导需要移除的项
    for(int i = 0;i < oldLen;i++) {
        jobject elementObj = env->GetObjectArrayElement(dexElements, i);

        dalvik_system_DexPathList::Element element(env,elementObj);
        jobject fileObj = element.getPath();
        java_io_File javaIoFile(env,fileObj);
        jstring fileName = javaIoFile.getName();
        if(fileName == nullptr){
            DLOGW("removeDexElements got an empty file name");
            continue;
        }
        const char* fileNameChs = env->GetStringUTFChars(fileName,nullptr);
        DLOGD("removeDexElements[%d] old path = %s",i,fileNameChs);

        if(strncmp(fileNameChs,removeElementNameChs,256) == 0){
            newLen--;
        }
        env->ReleaseStringUTFChars(fileName,fileNameChs);
    }

    dalvik_system_DexPathList::Element arrayElement(env, nullptr);
    jclass ElementClass = arrayElement.getClass();
    jobjectArray newElementArray = env->NewObjectArray(newLen,ElementClass,nullptr);

    DLOGD("removeDexElements oldlen = %d , newlen = %d",oldLen,newLen);

    jint newArrayIndex = 0;
    //填充新数组
    for(int i = 0;i < oldLen;i++) {
        jobject elementObj = env->GetObjectArrayElement(dexElements, i);

        dalvik_system_DexPathList::Element element(env,elementObj);
        jobject fileObj = element.getPath();
        java_io_File javaIoFile(env,fileObj);
        jstring fileName = javaIoFile.getName();
        if(fileName == nullptr){
            DLOGW("removeDexElements got an empty file name");
            continue;
        }
        const char* fileNameChs = env->GetStringUTFChars(fileName,nullptr);

        if(strncmp(fileNameChs,removeElementNameChs,256) == 0){
            DLOGD("removeDexElements will remove item: %s",fileNameChs);
            env->ReleaseStringUTFChars(fileName,fileNameChs);
            continue;
        }
        env->ReleaseStringUTFChars(fileName,fileNameChs);

        env->SetObjectArrayElement(newElementArray,newArrayIndex++,elementObj);
    }

    dexPathList.setDexElements(newElementArray);
    DLOGD("removeDexElements success");
}

jstring readAppComponentFactory(JNIEnv *env, jclass __unused) {
    int64_t entry_size;
    if(zip_addr == nullptr){
        char apkPathChs[256] = {0};
        getApkPath(env,apkPathChs,256);
        load_zip(apkPathChs,&zip_addr,&zip_size);
    }

    if(appComponentFactoryChs == nullptr) {
        appComponentFactoryChs = (char*)read_zip_file_entry(zip_addr, zip_size,ACF_NAME_IN_ZIP, &entry_size);
    }
    DLOGD("readAppComponentFactory = %s", appComponentFactoryChs);
    return env->NewStringUTF((appComponentFactoryChs));
}

jstring readApplicationName(JNIEnv *env, jclass __unused) {
    int64_t entry_size;
    if(zip_addr == nullptr){
        char apkPathChs[256] = {0};
        getApkPath(env,apkPathChs,256);
        load_zip(apkPathChs,&zip_addr,&zip_size);
    }

    if(applicationNameChs == nullptr) {
        applicationNameChs = (char*)read_zip_file_entry(zip_addr, zip_size,APP_NAME_IN_ZIP, &entry_size);
    }
    DLOGD("readApplicationName = %s", applicationNameChs);
    return env->NewStringUTF((applicationNameChs));
}

void init_dpt() {
    DLOGI("init_dpt call!");
    dpt_hook();
}

jclass getRealApplicationClass(JNIEnv *env, const char *applicationClassName) {
    if (g_realApplicationClass == nullptr) {
        jclass applicationClass = env->FindClass(applicationClassName);
        g_realApplicationClass = (jclass) env->NewGlobalRef(applicationClass);
    }
    return g_realApplicationClass;
}

jobject getApplicationInstance(JNIEnv *env, jstring applicationClassName) {
    if (g_realApplicationInstance == nullptr) {
        const char *applicationClassNameChs = env->GetStringUTFChars(applicationClassName, nullptr);

        size_t len = strnlen(applicationClassNameChs,128) + 1;
        char *appNameChs = static_cast<char *>(calloc(len, 1));
        parseClassName(applicationClassNameChs, appNameChs);

        DLOGD("getApplicationInstance %s -> %s",applicationClassNameChs,appNameChs);


        jclass appClass = getRealApplicationClass(env, appNameChs);
        jmethodID _init = env->GetMethodID(appClass, "<init>", "()V");
        jobject appInstance = env->NewObject(appClass, _init);
        if (env->ExceptionCheck() || nullptr == appInstance) {
            env->ExceptionClear();
            DLOGW("getApplicationInstance fail!");
            return nullptr;
        }
        g_realApplicationInstance = env->NewGlobalRef(appInstance);

        free(appNameChs);
        DLOGD("getApplicationInstance success!");

    }
    return g_realApplicationInstance;
}

void callRealApplicationOnCreate(JNIEnv *env, jclass, jstring realApplicationClassName) {

    jobject appInstance = getApplicationInstance(env,realApplicationClassName);
    android_app_Application application(env,appInstance);
    application.onCreate();

    DLOGD("callRealApplicationOnCreate call success!");

}

void callRealApplicationAttach(JNIEnv *env, jclass, jobject context,
                                         jstring realApplicationClassName) {

    jobject appInstance = getApplicationInstance(env,realApplicationClassName);

    android_app_Application application(env,appInstance);
    application.attach(context);

    DLOGD("callRealApplicationAttach call success!");

}

void replaceApplication(JNIEnv *env, jclass klass, jstring realApplicationClassName){

    jobject appInstance = getApplicationInstance(env, realApplicationClassName);
    if (appInstance == nullptr) {
        DLOGW("replaceApplication getApplicationInstance fail!");
        return;
    }
    replaceApplicationOnLoadedApk(env,klass,appInstance);
    replaceApplicationOnActivityThread(env,klass,appInstance);
    DLOGD("replace application success");
}

void replaceApplicationOnActivityThread(JNIEnv *env,jclass __unused, jobject realApplication){
    android_app_ActivityThread activityThread(env);
    activityThread.setInitialApplication(realApplication);
    DLOGD("replaceApplicationOnActivityThread success");
}

void replaceApplicationOnLoadedApk(JNIEnv *env, jclass __unused,jobject realApplication) {
    android_app_ActivityThread activityThread(env);

    jobject mBoundApplicationObj = activityThread.getBoundApplication();

    android_app_ActivityThread::AppBindData appBindData(env,mBoundApplicationObj);
    jobject loadedApkObj = appBindData.getInfo();

    android_app_LoadedApk loadedApk(env,loadedApkObj);

    //make it null
    loadedApk.setApplication(nullptr);

    jobject mAllApplicationsObj = activityThread.getAllApplication();

    java_util_ArrayList arrayList(env,mAllApplicationsObj);

    jobject removed = (jobject)arrayList.remove(0);
    if(removed != nullptr){
        DLOGD("replaceApplicationOnLoadedApk proxy application removed");
    }

    jobject ApplicationInfoObj = loadedApk.getApplicationInfo();

    android_content_pm_ApplicationInfo applicationInfo(env,ApplicationInfoObj);

    char applicationName[128] = {0};
    getClassName(env,realApplication,applicationName);

    DLOGD("applicationName = %s",applicationName);
    char realApplicationNameChs[128] = {0};
    parseClassName(applicationName,realApplicationNameChs);
    jstring realApplicationName = env->NewStringUTF(realApplicationNameChs);
    auto realApplicationNameGlobal = (jstring)env->NewGlobalRef(realApplicationName);

    android_content_pm_ApplicationInfo appInfo(env,appBindData.getAppInfo());

    //replace class name
    applicationInfo.setClassName(realApplicationNameGlobal);
    appInfo.setClassName(realApplicationNameGlobal);

    DLOGD("replaceApplicationOnLoadedApk begin makeApplication!");

    // call make application
    loadedApk.makeApplication(JNI_FALSE,nullptr);

    DLOGD("replaceApplicationOnLoadedApk success!");
}


static bool registerNativeMethods(JNIEnv *env) {
    jclass JniBridgeClass = env->FindClass("com/luoyesiqiu/shell/JniBridge");
    if (env->RegisterNatives(JniBridgeClass, gMethods, sizeof(gMethods) / sizeof(gMethods[0])) ==
        0) {
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

static void loadApk(JNIEnv *env){
    char apkPathChs[256] = {0};
    getApkPath(env,apkPathChs,256);

    if(zip_addr == nullptr){
        load_zip(apkPathChs,&zip_addr,&zip_size);
    }
}

static void writeDexAchieve(const char *dexAchievePath) {
    int64_t dex_files_size = 0;
    void *dexFilesData = read_zip_file_entry(zip_addr,zip_size,DEX_FILES_NAME_IN_ZIP,&dex_files_size);
    DLOGD("zipCode open = %s",dexAchievePath);
    int fd = open(dexAchievePath, O_CREAT | O_WRONLY  ,S_IRWXU);
    if(fd > 0){
        write(fd,dexFilesData,dex_files_size);
        close(fd);
    }
    else {
        DLOGE("WTF! zipCode write fail: %s", strerror(fd));
    }
}

static void extractDexes(){
    char compressedDexesPathChs[256] = {0};
    getCompressedDexesPath(compressedDexesPathChs, 256);

    char codeCachePathChs[256] = {0};
    getCodeCachePath(codeCachePathChs, 256);

    if(access(codeCachePathChs, F_OK) == 0){
        if(access(compressedDexesPathChs, F_OK) != 0) {
            writeDexAchieve(compressedDexesPathChs);
        }
        else {
            DLOGI("dex files is achieved!");
        }
    }
    else {
        if(mkdir(codeCachePathChs,0775) == 0){
            writeDexAchieve(compressedDexesPathChs);
        }
        else {
            DLOGE("WTF! cannot make code_cache directory!");
        }
    }
}

void init_app(JNIEnv *env, jclass __unused, jobject context) {
    DLOGD("init_app!");
    clock_t start = clock();

    loadApk(env);
    extractDexes();

    if (nullptr == context) {
        int64_t entry_size = 0;
        if(codeItemFilePtr == nullptr) {
            codeItemFilePtr = read_zip_file_entry(zip_addr,zip_size,CODE_ITEM_NAME_IN_ZIP,&entry_size);
        }
        readCodeItem((uint8_t*)codeItemFilePtr,entry_size);

    } else {
        AAsset *aAsset = getAsset(env, context, CODE_ITEM_NAME_IN_ASSETS);
        if (aAsset != nullptr) {
            int len = AAsset_getLength(aAsset);
            auto buf = (uint8_t *) AAsset_getBuffer(aAsset);
            readCodeItem(buf,len);
        }
    }
    printTime("read apk data took =" , start);
}

void readCodeItem(uint8_t *data,size_t data_len) {

    if (data != nullptr && data_len >= 0) {

        data::MultiDexCode *dexCode = data::MultiDexCode::getInst();

        dexCode->init(data, data_len);
        DLOGI("readCodeItem : version = %d , dexCount = %d", dexCode->readVersion(),
              dexCode->readDexCount());
        int indexCount = 0;
        uint32_t *dexCodeIndex = dexCode->readDexCodeIndex(&indexCount);
        for (int i = 0; i < indexCount; i++) {
            DLOGI("readCodeItem : dexCodeIndex[%d] = %d", i, *(dexCodeIndex + i));
            uint32_t dexCodeOffset = *(dexCodeIndex + i);
            uint16_t methodCount = dexCode->readUInt16(dexCodeOffset);

            DLOGD("readCodeItem : dexCodeOffset[%d] = %d,methodCount[%d] = %d", i, dexCodeOffset, i,
                  methodCount);
            auto codeItemMap = new std::unordered_map<int, data::CodeItem *>();
            uint32_t codeItemIndex = dexCodeOffset + 2;
            for (int k = 0; k < methodCount; k++) {
                data::CodeItem *codeItem = dexCode->nextCodeItem(&codeItemIndex);
                uint32_t methodIdx = codeItem->getMethodIdx();
                codeItemMap->insert(std::pair<int, data::CodeItem *>(methodIdx, codeItem));
            }
            dexMap.insert(std::pair<int, std::unordered_map<int, data::CodeItem *> *>(i, codeItemMap));

        }
        DLOGD("readCodeItem map size = %lu", (unsigned long)dexMap.size());
    }
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *__unused) {

    JNIEnv *env = nullptr;
    if (vm->GetEnv((void **) &env, JNI_VERSION_1_4) != JNI_OK) {
        return JNI_ERR;
    }

    if (registerNativeMethods(env) == JNI_FALSE) {
        return JNI_ERR;
    }

    DLOGI("JNI_OnLoad called!");
    return JNI_VERSION_1_4;
}

