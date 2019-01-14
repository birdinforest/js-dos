#include <js-dos-ci.h>
#include <js-dos-events.h>
#include <js-dos-json.h>
#include <unordered_map>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE /*EMSCRIPTEN_KEEPALIVE*/
#endif

// send
// ----
// Send is low level api to communicate with CommandInterface, it is used by
// javascript. Javascript can use this method to send information into c++
// layer, like this:
// ```
//   Module['send']('event', msg, funciton callback(msg) {
//     // ...
//   });
// ```
// callback will be called only if 'event' is know for c++ layer, and only when
// c++ layer call callback so IT IS ASYNC!

// Types:
// * **send_callback_fn** - function that C++ code should call, to trigger
// javascript callback function (that passed to send method)
typedef void (*send_callback_fn)(const std::string &callback_name,
                                 const jsonstream &);
// * **send_handler_fn** - handler for javascript callbacks
typedef void (*send_handler_fn)(const std::string &callback_name,
                                const char *data, send_callback_fn callback_fn);

std::unordered_map<std::string, send_handler_fn> &getSendHandlers() {
  static std::unordered_map<std::string, send_handler_fn> sendHandlers;
  return sendHandlers;
}

// **_send** - is actual implemenation of Module['__send']
extern "C" void EMSCRIPTEN_KEEPALIVE _send(const char *ckey, const char *data,
                                           const char *ccallback) {
  jsonstream emptystream;
  static void (*fn)(const std::string &, const jsonstream &) =
      [](const std::string &callback, const jsonstream &stream) -> void {
#ifdef EMSCRIPTEN
    EM_ASM_ARGS(
        {
          var clfield = Pointer_stringify($0);
          var innerobj = Pointer_stringify($1);
          if (innerobj.length > 0) {
            innerobj = innerobj.slice(0, -1);
          }
          var object = JSON.parse('{' + innerobj + '}');
          if (clfield in Module) {
            Module[clfield](object);
            delete Module[clfield];
          }
        },
        callback.c_str(), stream.c_str());
#endif
  };

  std::string key(ckey);
  std::string callback(ccallback);

  auto sendHandlers = getSendHandlers();
  auto found = sendHandlers.find(key);
  if (found != sendHandlers.end()) {
    found->second(callback, data, fn);
    return;
  }

  printf("WARN! Can't find handler for key '%s'\n", ckey);
}

// **registerSendFn** - add send function to Module object
void registerSendFn() {
#ifdef EMSCRIPTEN
  EM_ASM(({
    Module['send'] = function(key, data, callback) {
      if (!callback) {
        callback = function(){};
      }

      if (!data) {
        data = "";
      }

      var clfield = key + '_callback_' + Math.random();

      var keyLength = Module['lengthBytesUTF8'](key) + 1;
      var clfieldLength = Module['lengthBytesUTF8'](clfield) + 1;
      var dataLength = Module['lengthBytesUTF8'](data) + 1;

      var clfieldBuffer = Module['_malloc'](clfieldLength);
      var keyBuffer = Module['_malloc'](keyLength);
      var dataBuffer = Module['_malloc'](dataLength);

      Module['stringToUTF8'](key, keyBuffer, keyLength);
      Module['stringToUTF8'](clfield, clfieldBuffer, clfieldLength);
      Module['stringToUTF8'](data, dataBuffer, dataLength);

      Module[clfield] = callback;
      Module['__send'](keyBuffer, dataBuffer, clfieldBuffer);
      Module['_free'](keyBuffer);
      Module['_free'](clfieldBuffer);
      Module['_free'](dataBuffer);
    };
  }));
#endif
}

// Ping
// ----
// Ping is opposite to send, it called by c++ to trigger something in javascript
void ping(const char *event) {
#ifdef EMSCRIPTEN
  EM_ASM(({
           const event = Pointer_stringify($0);
           Module['ping'](event);
         }),
         event);
#else
  printf("PING: %s\n", event);
#endif
}

Events::Events() {
  registerSendFn();
  registerExit();
  registerScreenshot();
}

void Events::ready() { ping("ready"); }

void Events::frame() {
  static long frameCount = 0;

  if (frameCount == 0) {
    // do nothing
  } else if (frameCount == 1) {
    ready();
  } else {
    ping("frame");
    supplyScreenshotIfNeeded();
  }

  frameCount++;
}

void Events::registerExit() {
  getSendHandlers().insert(std::make_pair<>(
      "exit", [](const std::string &callback_name, const char *data,
                 send_callback_fn callback_fn) {
        delete ci();
        callback_fn(callback_name, jsonstream());
      }));
}

void Events::registerScreenshot() {
  getSendHandlers().insert(std::make_pair<>(
      "screenshot", [](const std::string &callback_name, const char *data,
                       send_callback_fn callback_fn) {
#ifdef EMSCRIPTEN
        EM_ASM((const callbackName = Pointer_stringify($0);
                Module['screenshot_callback_name'] = callbackName;),
               callback_name.c_str());
#endif
      }));
}

void Events::supplyScreenshotIfNeeded() {
#ifdef EMSCRIPTEN
  EM_ASM(({
    if (!Module['screenshot_callback_name']) {
      return;
    }

    const callbackName = Module['screenshot_callback_name'];
    const imgData = Module['canvas'].toDataURL("image/png");
    const callback = Module[callbackName];
    delete Module[callbackName];
    delete Module['screenshot_callback_name'];
    callback(imgData);
  }));
#endif
}