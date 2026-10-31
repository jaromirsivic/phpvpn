package phpvpn.android.bridge

import android.os.ParcelFileDescriptor
import androidx.test.platform.app.InstrumentationRegistry

/**
 * Runs a shell script as the adb shell user (uid 2000) through the
 * UiAutomation connection of `am instrument`. Like `adb shell`, uid 2000 is an
 * ordinary user of the VPN routing rules, so its traffic stands in for any
 * other app (plan-android-v001 T-A01b); the test app itself is excluded from
 * the VPN by addDisallowedApplication and cannot serve as that other app.
 */
object ShellUser {
    fun run(script: String): String {
        val automation = InstrumentationRegistry.getInstrumentation().uiAutomation
        // [0] stdout, [1] stdin, [2] stderr; the script merges stderr itself.
        val streams = automation.executeShellCommandRwe("sh")
        ParcelFileDescriptor.AutoCloseInputStream(streams[2]).close()
        ParcelFileDescriptor.AutoCloseOutputStream(streams[1]).use { input ->
            input.write("exec 2>&1\n$script\nexit 0\n".toByteArray(Charsets.UTF_8))
        }
        return ParcelFileDescriptor.AutoCloseInputStream(streams[0]).use { output ->
            output.readBytes().toString(Charsets.UTF_8)
        }
    }

    /** "key=value" lines of [run] as a map. */
    fun values(output: String): Map<String, String> = output.lineSequence()
        .mapNotNull { line ->
            val separator = line.indexOf('=')
            if (separator <= 0) null else line.substring(0, separator).trim() to line.substring(separator + 1).trim()
        }
        .toMap()
}
