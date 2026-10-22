using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

namespace SilentHillPC_Launcher
{
    /// <summary>Silent Hill PC Randomizer: the randomizer add-on's settings
    /// (randomizer.cfg), kept apart from the main launcher. Sits next to
    /// SilentHillPC.exe. The code still calls it Remix, its first name.</summary>
    static class Program
    {
        static bool IsLegacy(ConfigManager game)
        {
            return game.Get("spu_renderer", "authentic").Trim().Equals("legacy", StringComparison.OrdinalIgnoreCase);
        }

        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            string dir       = AppDomain.CurrentDomain.BaseDirectory;
            string remixPath = Path.Combine(dir, "randomizer.cfg");
            string oldPath   = Path.Combine(dir, "remix.cfg");
            string gamePath  = Path.Combine(dir, "config.cfg");
            string exePath   = Path.Combine(dir, "SilentHillPC.exe");
            string dllPath   = Path.Combine(dir, "SilentHillPC_RandomizerAddon.dll");

            if (!File.Exists(dllPath))
                MessageBox.Show("SilentHillPC_RandomizerAddon.dll is not next to SilentHillPC_Randomizer.exe.\n" +
                                "These settings are saved, but the game ignores them until the add-on DLL " +
                                "is in the game's folder (beside SilentHillPC.exe).",
                                "Silent Hill PC Randomizer", MessageBoxButtons.OK, MessageBoxIcon.Warning);

            /* Settings saved as "Remix" (remix.cfg) carry over once, as the add-on
             * itself does it. */
            if (!File.Exists(remixPath) && File.Exists(oldPath))
            {
                try { File.Copy(oldPath, remixPath); } catch (Exception) { /* the add-on copies it on its next start */ }
            }
            /* Before the first settings file, an older build's remix_* keys may still
             * be in config.cfg: start from those, as the add-on itself does. */
            bool fresh  = !File.Exists(remixPath);
            var  game   = new ConfigManager(gamePath);
            /* A monster away from its home area plays its own sounds only if they
             * fit in sound memory: the default renderer has the PSX's 512 KiB, the
             * legacy one 2 MiB. Set whenever the launcher opens, whatever is saved. */
            if (!IsLegacy(game) && File.Exists(gamePath))
            {
                game.Set("spu_renderer", "legacy");
                game.Save();
            }
            var  config = fresh ? game : new ConfigManager(remixPath);
            var  loaded = RemixSettings.Load(config);
            using (var form = new RemixSettingsForm(loaded.Clone(), File.Exists(exePath)))
            {
                if (form.ShowDialog() != DialogResult.OK || form.Result == null)
                    return;
                /* Read again just before writing: the game may have changed keys
                 * (PC Settings' randomizer page) while this window was open. */
                fresh  = !File.Exists(remixPath);
                config = new ConfigManager(remixPath);
                if (fresh)
                    File.WriteAllText(remixPath, "# Randomizer settings (SilentHillPC_RandomizerAddon.dll), written by the Randomizer launcher.\r\n" +
                                                 "# See pc_port/docs/Randomizer_Mode.md.\r\n");
                form.Result.Save(config, fresh ? null : RemixSettings.Load(config));
                config.Save();

                if (!form.PlayRequested)
                    return;
                try
                {
                    Process.Start(new ProcessStartInfo(exePath) { WorkingDirectory = dir });
                }
                catch (Exception ex)
                {
                    MessageBox.Show("Failed to launch SilentHillPC.exe: " + ex.Message, "Silent Hill PC Randomizer");
                }
            }
        }
    }
}
