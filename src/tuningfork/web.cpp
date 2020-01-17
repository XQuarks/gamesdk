/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "web.h"

#include "jni_wrap.h"

#include <sstream>

#define LOG_TAG "TuningFork:Web"
#include "Log.h"

#include "tuningfork_utils.h"

namespace tuningfork {

std::string Request::GetURL(std::string rpcname) const {
    std::stringstream url;
    url << base_url_;
    url << json_utils::GetResourceName(info_);
    url << rpcname;
    return url.str();
}

TFErrorCode Request::Send(const std::string& rpc_name, const std::string& request,
                          int& response_code, std::string& response_body) {
    return TFERROR_OK;
}

WebRequest::WebRequest(const Request& inner) :
        Request(inner) {
}

WebRequest::WebRequest(const WebRequest& rhs) :
        Request(rhs) {
}


TFErrorCode WebRequest::Send(const std::string& rpc_name, const std::string& request_json,
                 int& response_code, std::string& response_body) {
    if (!jni::IsValid()) return TFERROR_JNI_BAD_ENV;
    auto uri = GetURL(rpc_name);
    ALOGI("Connecting to: %s", uri.c_str());

    using namespace jni;

    auto url = java::net::URL(uri);
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION); // Malformed URL

    // Open connection and set properties
    java::net::HttpURLConnection connection(url.openConnection());
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION);// IOException
    connection.setRequestMethod("POST");
    auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout_).count();
    connection.setConnectTimeout(timeout_ms);
    connection.setReadTimeout(timeout_ms);
    connection.setDoOutput(true);
    connection.setDoInput(true);
    connection.setUseCaches(false);
    if (!api_key_.empty()) {
        connection.setRequestProperty( "X-Goog-Api-Key", api_key_);
    }
    connection.setRequestProperty( "Content-Type", "application/json");

    std::string package_name;
    apk_utils::GetVersionCode(&package_name);
    if (!package_name.empty())
      connection.setRequestProperty( "X-Android-Package", package_name);
    auto signature = apk_utils::GetSignature();
    if (!signature.empty())
      connection.setRequestProperty( "X-Android-Cert", signature);

    // Write json request body
    auto os = connection.getOutputStream();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION); // IOException
    auto writer = java::io::BufferedWriter(java::io::OutputStreamWriter(os, "UTF-8"));
    writer.write(request_json);
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION);// IOException
    writer.flush();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION);// IOException
    writer.close();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION);// IOException
    os.close();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION);// IOException

    // Connect and get response
    connection.connect();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION);// IOException

    response_code = connection.getResponseCode();
    ALOGI("Response code: %d", response_code);
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION);// IOException

    auto resp = connection.getResponseMessage();
    ALOGI("Response message: %s", resp.C());
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION);// IOException

    // Read body from input stream
    auto is = connection.getInputStream();
    CHECK_FOR_JNI_EXCEPTION_AND_RETURN(TFERROR_JNI_EXCEPTION);// IOException
    auto reader = java::io::BufferedReader(java::io::InputStreamReader(is, "UTF-8"));
    std::stringstream body;
    while (true) {
        auto line = reader.readLine();
        if (line.J()==nullptr) break;
        body << line.C() << "\n";
    }

    reader.close();
    is.close();
    connection.disconnect();

    response_body = body.str();

    return TFERROR_OK;
}

} // namespace tuningfork
