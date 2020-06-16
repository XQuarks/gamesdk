package com.google.androidgamesdk

import com.google.androidgamesdk.OsSpecificTools.Companion.joinPath
import com.google.androidgamesdk.OsSpecificTools.Companion.osExecutableSuffix
import com.google.androidgamesdk.OsSpecificTools.Companion.osFolderName
import org.gradle.api.GradleException
import org.gradle.api.Project
import java.io.File
import java.util.*
import java.util.regex.Pattern


abstract class Toolchain {
    abstract protected var project_: Project;
    abstract protected var androidVersion_: String
    abstract protected var ndkVersion_: String
    abstract fun getAndroidNDKPath(): String;
    fun getAndroidVersion(): String { return androidVersion_; }
    fun getNdkVersion(): String { return ndkVersion_; }
    fun getNdkVersionNumber(): String {
        return extractNdkMajorVersion(ndkVersion_)
    }
    fun getBuildKey(arch: String, stl: String, buildType: String): String {
        return arch + "_API" + androidVersion_ + "_NDK" + getNdkVersionNumber() + '_' +
                sanitize(stl)+ '_'+buildType
    }
    protected fun getNdkVersionFromPropertiesFile(): String {
        val file = File(getAndroidNDKPath(), "source.properties")
        if (!file.exists()) {
            println("Warning: can't get NDK version from source.properties")
            return "UNKNOWN"
        }
        else {
            val props = loadPropertiesFromFile(file)
            val ver = props["Pkg.Revision"]
            if (ver is String) {
                return extractNdkMajorVersion(ver)
            }
            println("Warning: can't get NDK version from source.properties (Pkg.Revision is not a string)")
            return "UNKNOWN"
        }
    }
    private fun sanitize(s: String): String {
        return s.replace('+', 'p')
    }
    fun findNDKTool(name: String): String {
        val searchedPath = joinPath(getAndroidNDKPath(), "toolchains")
        val tools = project_.fileTree(searchedPath).matching {
            include("**/bin/" + name + osExecutableSuffix())
            // Be sure not to use tools that would not support architectures for which we're building:
            exclude("**/aarch64-*", "**/arm-*")
        }
        if (tools.isEmpty) {
            throw GradleException("No $name found in $searchedPath")
        }
        return tools.getFiles().last().toString()
    }
    fun getArPath(): String {
        return findNDKTool("ar")
    }
    fun getCMakePath(): String  {
        return File("${project_.projectDir}/../prebuilts/cmake/"
                + osFolderName(ExternalToolName.CMAKE)
                + "/bin/cmake" + osExecutableSuffix()).getPath()
    }
    fun getNinjaPath(): String  {
        return File("${project_.projectDir}/../prebuilts/cmake/"
                + osFolderName(ExternalToolName.CMAKE)
                + "/bin/ninja" + osExecutableSuffix()).getPath()
    }

    protected fun extractNdkMajorVersion(ndkVersion: String): String {
        val majorVersionPattern = Pattern.compile("[0-9]+")
        val matcher = majorVersionPattern.matcher(ndkVersion)
        if (matcher.find()) {
            return matcher.group();
        }
        return "UNKNOWN"
    }

    protected fun loadPropertiesFromFile(file: File): Properties {
        val props = Properties()
        val inputStream = file.inputStream()
        props.load(inputStream)
        inputStream.close();

        return props;
    }
}