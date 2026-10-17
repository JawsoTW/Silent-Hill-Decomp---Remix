using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;

namespace SilentHillPC_Launcher
{
    /// <summary>Silent Hill PC Remix: the Remix add-on's settings (remix.cfg),
    /// kept apart from the main launcher. Sits next to SilentHillPC.exe.</summary>
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
            string remixPath = Path.Combine(dir, "remix.cfg");
            string gamePath  = Path.Combine(dir, "config.cfg");
            string exePath   = Path.Combine(dir, "SilentHillPC.exe");
            string dllPath   = Path.Combine(dir, "SilentHillPC_RemixAddon.dll");

            if (!File.Exists(dllPath))
                MessageBox.Show("SilentHillPC_RemixAddon.dll is not next to SilentHillPC_Remix.exe.\n" +
                                "These settings are saved, but the game ignores them until the add-on DLL " +
                                "is in the game's folder (beside SilentHillPC.exe).",
                                "Silent Hill PC Remix", MessageBoxButtons.OK, MessageBoxIcon.Warning);

            /* Before the first remix.cfg, an older build's remix_* keys may still
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
            using (var form = new RemixSettingsForm(loaded.Clone(), game.Get("randomizer", "0") == "1",
                                                    File.Exists(exePath)))
            {
                if (form.ShowDialog() != DialogResult.OK || form.Result == null)
                    return;
                /* Read again just before writing: the game may have changed keys
                 * (PC Settings' Remix page) while this window was open. */
                fresh  = !File.Exists(remixPath);
                config = new ConfigManager(remixPath);
                if (fresh)
                    File.WriteAllText(remixPath, "# Remix add-on settings (SilentHillPC_RemixAddon.dll), written by the Remix launcher.\r\n" +
                                                 "# See pc_port/docs/Remix_Mode.md.\r\n");
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
                    MessageBox.Show("Failed to launch SilentHillPC.exe: " + ex.Message, "Silent Hill PC Remix");
                }
            }
        }
    }
}
