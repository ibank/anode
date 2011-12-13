#include "Env.h"

#include "defines.h"
#include <pthread.h>

#include "VM.h"
#ifndef ANDROID
# include "JREVM.h"
#endif
#include "Conv.h"
#include "Interface.h"
#include "Utils.h"

static pthread_key_t key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
static void static_init() {
	pthread_key_create(&key, 0);
#ifndef ANDROID
	/* JRE VM */
	int result = JREVM::static_init();
	if(result != OK) {
		/* fatal */
		fprintf(stderr, "Fatal error in static initialisation of Env\n");
	}
#endif //ANDROID
}

using namespace v8;

#ifdef ANDROID
/* preemptively called when starting a node instance
 * so the reactive initEnv() won't occur*/
void Env::setupEnv(VM *vm) {
  static_init();
	initEnv(vm);
}
#endif

/* called on the first occasion that the Env is obtained in a process */
Env *Env::initEnv(VM *vm) {
	Env *result = new Env(vm);
	pthread_setspecific(key, result);
	return result;
}

Env *Env::getEnv_nocheck() {
	return (Env *)pthread_getspecific(key);
}

Env *Env::getEnv() {
	pthread_once(&key_once, static_init);
	Env *result;
	if((result = getEnv_nocheck()) == 0) {
		result = initEnv(0);
	}
	return result;
}

Env::Env(VM *vm) {
	nodeIsolate = node::Isolate::GetCurrent();
	v8Isolate = v8::Isolate::GetCurrent();
#ifndef ANDROID
	if(!vm)
		vm = new JREVM();
#endif
	this->vm = vm;
  conv = new Conv(this, vm->getJNIEnv());
  interfaces = TArray<Interface*>::New();
  initJava(nodeIsolate);
}

Env::~Env() {
  JNIEnv *jniEnv = vm->getJNIEnv();
  jniEnv->CallVoidMethod(jEnv, releaseMethodId);
  jniEnv->DeleteGlobalRef(jEnv);
  jniEnv->DeleteGlobalRef(jEnvClass);
  conv->dispose(jniEnv);
  delete conv;
  for(int i = 0; i < interfaces->getLength(); i++) interfaces->get(i)->dispose(jniEnv);
  delete interfaces;
	delete vm;
}

int Env::initJava(node::Isolate *nodeIsolate) {
  int result = OK;
  nodeIsolate->exitHandler = Env::atExit;
  JNIEnv *jniEnv = vm->getJNIEnv();
  jEnvClass = (jclass)(jniEnv->NewGlobalRef(jniEnv->FindClass("org/meshpoint/anode/bridge/Env")));
  createMethodId = jniEnv->GetStaticMethodID(jEnvClass, "create", "(J)Lorg/meshpoint/anode/bridge/Env;");
  releaseMethodId = jniEnv->GetMethodID(jEnvClass, "release", "()V");
  jEnv = jniEnv->NewGlobalRef(jniEnv->CallStaticObjectMethod(jEnvClass, createMethodId,  (jlong)this));
  loadMethodId = jniEnv->GetMethodID(jEnvClass, "loadModule", "(Ljava/lang/String;Lorg/meshpoint/anode/bridge/ModuleContext;)Ljava/lang/Object;");
  
  if(jniEnv->ExceptionCheck()) {
    result = ErrorVM;
    jniEnv->ExceptionClear();
  }
  return result;
}

void Env::atExit() {
  delete getEnv_nocheck();
  pthread_setspecific(key, 0);
}
  
Local<Value> Env::load(Handle<String> moduleName, Handle<Object> moduleExports) {
  HandleScope scope;
	Local<Value> module;
  
  /* wrap the exports object */
  jobject jExports;
  JNIEnv *jniEnv = vm->getJNIEnv();
  int result = conv->ToJavaObject(jniEnv, moduleExports, TYPE_OBJECT, &jExports);
  if(result != OK) {
    fprintf(stderr, "Fatal error: unable to convert module exports object\n");
    return scope.Close(Undefined());
  }
    
  /* convert the moduleName to jstring */
  jstring jModuleName;
  result = Conv::ToJavaString(jniEnv, moduleName, &jModuleName);
  if(result != OK) {
    fprintf(stderr, "Fatal error: unable to convert modulename string\n");
    return scope.Close(Undefined());
  }

  /* create the module context */
  jobject jCtx;
  result = vm->createContext(jEnv, jExports, &jCtx);
  if(result == OK) {
    jobject jModule = jniEnv->CallObjectMethod(jEnv, loadMethodId, jModuleName, jCtx);
    if(jModule) {
      /* wrap this according to its type */
    }
  }
	return scope.Close(module);
}
