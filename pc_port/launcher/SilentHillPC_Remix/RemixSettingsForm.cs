using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

namespace SilentHillPC_Launcher
{
    /// <summary>The keys of remix.cfg (see pc_port/docs/Remix_Mode.md).</summary>
    public class RemixSettings
    {
        public bool   Rooms;
        public bool   Items;
        public bool   Keys;
        public bool   Enemies;
        public bool   StarterWeapons;
        public bool   Weapons;
        public bool   Unlockables;
        public uint   Seed;
        /// <summary>Eight digits, as the game's REMIX_SEED_MAX.</summary>
        public const uint SeedMax = 99999999;
        public string Maps = "";

        /* Pickup type weights, in percent of their kind (each kind sums to 100). */
        public static readonly string[] WeightKeys =
        {
            "remix_weight_health_drink", "remix_weight_first_aid", "remix_weight_ampoule",
            "remix_weight_handgun_ammo", "remix_weight_shotgun_ammo", "remix_weight_rifle_ammo"
        };
        public static readonly string[] WeightNames =
        {
            "Health Drink", "First Aid Kit", "Ampoule",
            "Handgun bullets", "Shotgun shells", "Rifle shells"
        };
        /* The vanilla game's own proportions, rounded (pc_config.c's defaults). */
        public static readonly int[] DefaultWeights = { 69, 27, 4, 67, 19, 14 };
        public int[] Weights = (int[])DefaultWeights.Clone();

        /* Remix enemies per kind of area: the monsters a spawn there may become. */
        public static readonly string[] ZoneKeys =
        {
            "outside", "outside_otherworld", "school", "otherworld_school",
            "hospital", "buildings", "nowhere", "park", "sewers"
        };
        public static readonly string[] ZoneNames =
        {
            "Outside", "Outside\nOtherworld", "School", "Otherworld\nSchool",
            "Hospital", "Other\nbuildings", "Nowhere", "Amusement\nPark", "Sewers"
        };
        public const int ZoneSewers = 8;
        public static readonly string[] EnemyKeys =
        {
            "GreyChild", "Mumbler", "Creeper", "Groaner", "Wormhead", "Romper", "AirScreamer",
            "NightFlutter", "LarvalStalker", "Stalker", "PuppetNurse", "PuppetDoctor", "HangedScratcher"
        };
        public static readonly string[] EnemyNames =
        {
            "Grey Child", "Mumbler", "Creeper", "Groaner", "Wormhead", "Romper", "Air Screamer",
            "Night Flutter", "Larval Stalker", "Stalker", "Puppet Nurse", "Puppet Doctor", "Hanged Scratcher"
        };
        /* The game's built-in mixes (pc_remix.c ENEMY_POOL_*); the hospital and the
         * sewers show both worlds' mixes together. Left like this, a zone is written as
         * "default" and keeps its built-in mix, world by world. */
        public static readonly string[][] DefaultPools =
        {
            new[] { "AirScreamer", "Groaner", "Romper", "Creeper" },
            new[] { "NightFlutter", "Wormhead", "Romper", "Creeper" },
            new[] { "GreyChild", "Mumbler", "Creeper", "Groaner", "PuppetNurse", "PuppetDoctor", "LarvalStalker" },
            new[] { "GreyChild", "Mumbler", "Creeper", "Wormhead", "PuppetNurse", "PuppetDoctor" },
            new[] { "GreyChild", "Mumbler", "Creeper", "Groaner", "Wormhead", "PuppetNurse", "PuppetDoctor" },
            new[] { "GreyChild", "Mumbler", "Creeper", "Groaner", "PuppetNurse", "PuppetDoctor" },
            new[] { "Stalker", "Creeper", "Wormhead", "PuppetNurse", "PuppetDoctor" },
            new[] { "GreyChild", "Mumbler", "Romper", "Creeper", "Wormhead", "NightFlutter", "LarvalStalker" },
            new[] { "Creeper", "HangedScratcher", "Groaner", "Romper", "Mumbler", "Wormhead" },
        };
        /* Per zone: "default", "none" or a comma-separated list of EnemyKeys. */
        public string[] Pools = ZoneKeys.Select(z => "default").ToArray();
        public bool SewerScratchers;

        /* The items the remix may move, by section: { config name, shown name }.
         * remix_fixed_items lists the ones that stay at their normal spots. */
        public static readonly string[][] WeaponItems =
        {
            new[] { "SteelPipe", "Steel Pipe" }, new[] { "Hammer", "Hammer" }, new[] { "Axe", "Axe" },
            new[] { "Shotgun", "Shotgun" }, new[] { "HuntingRifle", "Hunting Rifle" },
        };
        public static readonly string[][] KeyItems =
        {
            new[] { "KeyOfLion", "Key of [Lion]" }, new[] { "KeyOfWoodman", "Key of [Woodman]" },
            new[] { "KeyOfScarecrow", "Key of [Scarecrow]" }, new[] { "Chemical", "Chemical" },
            new[] { "PictureCard", "Picture Card" }, new[] { "RubberBall", "Rubber Ball" },
            new[] { "PlateOfTurtle", "Plate of [Turtle]" }, new[] { "PlateOfHatter", "Plate of [Hatter]" },
            new[] { "PlateOfCat", "Plate of [Cat]" }, new[] { "PlateOfQueen", "Plate of [Queen]" },
            new[] { "BloodPack", "Blood Pack" }, new[] { "DisinfectingAlcohol", "Disinfecting Alcohol" },
            new[] { "Lighter", "Lighter" }, new[] { "BasementStoreroomKey", "Basement Storeroom Key" },
            new[] { "ExaminationRoomKey", "Examination Room Key" }, new[] { "VideoTape", "Video Tape" },
            new[] { "SafeKey", "Safe Key" }, new[] { "BirdCageKey", "Bird Cage Key" },
            new[] { "Pliers", "Pliers" }, new[] { "Screwdriver", "Screwdriver" }, new[] { "Camera", "Camera" },
            new[] { "RingOfContract", "Ring of Contract" }, new[] { "CrestOfMercury", "Crest of Mercury" },
            new[] { "Ankh", "Ankh" }, new[] { "DiskOfOuroboros", "Disk of Ouroboros" },
        };
        public static readonly string[][] UnlockableItems =
        {
            new[] { "Katana", "Katana" }, new[] { "ChannelingStone", "Channeling Stone" },
            new[] { "Chainsaw", "Chainsaw" }, new[] { "RockDrill", "Rock Drill" },
        };
        public static IEnumerable<string> AllItemKeys =>
            WeaponItems.Concat(KeyItems).Concat(UnlockableItems).Select(i => i[0]);

        public HashSet<string> Fixed = new HashSet<string>();

        /// <summary>remix_fixed_items as written: in list order, so an unchanged set reads back the same.</summary>
        public string FixedValue => string.Join(",", AllItemKeys.Where(Fixed.Contains));

        public bool AnyOn => Rooms || Items || Keys || Enemies || StarterWeapons || Weapons || Unlockables;

        /// <summary>The monsters a zone's value names.</summary>
        public static HashSet<string> PoolSet(int zone, string value)
        {
            var v = (value ?? "").Trim();
            if (v.Length == 0 || v.Equals("default", StringComparison.OrdinalIgnoreCase))
                return new HashSet<string>(DefaultPools[zone]);
            if (v.Equals("none", StringComparison.OrdinalIgnoreCase))
                return new HashSet<string>();
            return new HashSet<string>(v.Split(',').Select(s => s.Trim()).Where(s => EnemyKeys.Contains(s)));
        }

        /// <summary>A zone's set as written to remix.cfg.</summary>
        public static string PoolValue(int zone, HashSet<string> set)
        {
            if (set.Count == 0)
                return "none";
            if (set.SetEquals(DefaultPools[zone]))
                return "default";
            return string.Join(",", EnemyKeys.Where(set.Contains));
        }

        public static RemixSettings Load(ConfigManager config)
        {
            ulong raw;
            if (!ulong.TryParse(config.Get("remix_seed", "0").Trim(), out raw))
                raw = 0;
            uint seed = raw > SeedMax ? SeedMax : (uint)raw;
            var r = new RemixSettings
            {
                Rooms   = config.Get("remix", "0") == "1",
                Items   = config.Get("remix_items", "0") == "1",
                Keys    = config.Get("remix_keys", "0") == "1",
                Enemies = config.Get("remix_enemies", "0") == "1",
                StarterWeapons = config.Get("remix_starter_weapons", "0") == "1",
                Weapons        = config.Get("remix_weapons", "0") == "1",
                Unlockables    = config.Get("remix_unlockables", "0") == "1",
                Seed    = seed,
                Maps    = config.Get("remix_maps", "").Trim(),
                SewerScratchers = config.Get("remix_enemies_sewer_scratchers", "0") == "1"
            };
            for (int i = 0; i < WeightKeys.Length; i++)
            {
                int w;
                if (!int.TryParse(config.Get(WeightKeys[i], DefaultWeights[i].ToString()).Trim(), out w))
                    w = DefaultWeights[i];
                r.Weights[i] = Math.Max(0, Math.Min(100, w));
            }
            for (int z = 0; z < ZoneKeys.Length; z++)
                r.Pools[z] = PoolValue(z, PoolSet(z, config.Get("remix_enemies_" + ZoneKeys[z], "default")));
            var known = new HashSet<string>(AllItemKeys);
            foreach (var it in config.Get("remix_fixed_items", "").Split(','))
                if (known.Contains(it.Trim()))
                    r.Fixed.Add(it.Trim());
            return r;
        }

        /// <summary>Writes only what changed since `loaded`: the game's PC Options
        /// page edits the same keys, and an untouched launcher must not put back
        /// the values it read at startup.</summary>
        public void Save(ConfigManager config, RemixSettings loaded)
        {
            if (loaded == null || Rooms   != loaded.Rooms)   config.Set("remix",         Rooms   ? "1" : "0");
            if (loaded == null || Items   != loaded.Items)   config.Set("remix_items",   Items   ? "1" : "0");
            if (loaded == null || Keys    != loaded.Keys)    config.Set("remix_keys",    Keys    ? "1" : "0");
            if (loaded == null || Enemies != loaded.Enemies) config.Set("remix_enemies", Enemies ? "1" : "0");
            if (loaded == null || StarterWeapons != loaded.StarterWeapons)
                config.Set("remix_starter_weapons", StarterWeapons ? "1" : "0");
            if (loaded == null || Weapons != loaded.Weapons) config.Set("remix_weapons", Weapons ? "1" : "0");
            if (loaded == null || Unlockables != loaded.Unlockables)
                config.Set("remix_unlockables", Unlockables ? "1" : "0");
            if (loaded == null || FixedValue != loaded.FixedValue) config.Set("remix_fixed_items", FixedValue);
            if (loaded == null || Seed    != loaded.Seed)    config.Set("remix_seed",    Seed.ToString());
            if (loaded == null || (Maps ?? "") != (loaded.Maps ?? "")) config.Set("remix_maps", Maps ?? "");
            for (int i = 0; i < WeightKeys.Length; i++)
                if (loaded == null || Weights[i] != loaded.Weights[i])
                    config.Set(WeightKeys[i], Weights[i].ToString());
            for (int z = 0; z < ZoneKeys.Length; z++)
                if (loaded == null || Pools[z] != loaded.Pools[z])
                    config.Set("remix_enemies_" + ZoneKeys[z], Pools[z]);
            if (loaded == null || SewerScratchers != loaded.SewerScratchers)
                config.Set("remix_enemies_sewer_scratchers", SewerScratchers ? "1" : "0");
        }

        public RemixSettings Clone()
        {
            var c = (RemixSettings)MemberwiseClone();
            c.Weights = (int[])Weights.Clone();
            c.Pools   = (string[])Pools.Clone();
            c.Fixed   = new HashSet<string>(Fixed);
            return c;
        }
    }

    /// <summary>The Remix launcher's window: every Remix mode setting, seed included.</summary>
    public class RemixSettingsForm : Form
    {
        static readonly Color Back  = Color.FromArgb(32, 32, 32);
        static readonly Color Field = Color.FromArgb(48, 48, 48);
        static readonly Color Warn  = Color.Goldenrod;

        private readonly CheckBox      _rooms, _items, _keys, _enemies, _starterWeapons, _weapons, _unlockables;
        private readonly Dictionary<string, CheckBox> _move = new Dictionary<string, CheckBox>();
        private readonly NumericUpDown _seed;
        private readonly TextBox       _maps;
        private readonly TrackBar[]    _weight = new TrackBar[6];
        private readonly Label[]       _weightPct = new Label[6];
        private readonly CheckBox[,]   _pool = new CheckBox[RemixSettings.EnemyKeys.Length, RemixSettings.ZoneKeys.Length];
        private readonly CheckBox      _sewerScratchers;
        private readonly ToolTip       _tips = new ToolTip { AutoPopDelay = 15000 };
        private bool                   _balancing;

        public RemixSettings Result { get; private set; }
        /// <summary>Closed with Save &amp; Play: the caller starts the game after saving.</summary>
        public bool PlayRequested { get; private set; }

        public RemixSettingsForm(RemixSettings current, bool randomizerOn, bool canPlay)
        {
            Text            = Loc.T("Silent Hill PC Remix");
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { /* default icon */ }
            FormBorderStyle = FormBorderStyle.FixedDialog;
            StartPosition   = FormStartPosition.CenterScreen;
            MaximizeBox     = false;
            MinimizeBox     = false;
            ShowInTaskbar   = true;
            ClientSize      = new Size(760, 620);
            BackColor       = Back;
            ForeColor       = Color.Gainsboro;

            var tabs = new TabControl { Left = 6, Top = 6, Width = ClientSize.Width - 12, Height = ClientSize.Height - 54 };
            var tabGeneral = new TabPage(Loc.T("General"))    { BackColor = Back, ForeColor = Color.Gainsboro };
            var tabItems   = new TabPage(Loc.T("Item types")) { BackColor = Back, ForeColor = Color.Gainsboro };
            var tabEnemies = new TabPage(Loc.T("Enemies"))    { BackColor = Back, ForeColor = Color.Gainsboro };
            var tabMove    = new TabPage(Loc.T("Item List")) { BackColor = Back, ForeColor = Color.Gainsboro };
            tabs.TabPages.Add(tabGeneral);
            tabs.TabPages.Add(tabItems);
            tabs.TabPages.Add(tabMove);
            tabs.TabPages.Add(tabEnemies);
            Controls.Add(tabs);

            /* ---- General ---- */
            int y = 10;
            CheckBox Check(string text, string hint, bool value)
            {
                var c = new CheckBox { Text = text, Left = 12, Top = y, AutoSize = true, Checked = value };
                var h = new Label { Text = hint, Left = 30, Top = y + 20, Width = 690, Height = 30, ForeColor = Color.Gray };
                tabGeneral.Controls.Add(c);
                tabGeneral.Controls.Add(h);
                y += 54;
                return c;
            }

            _rooms = Check(Loc.T("Rooms"),
                Loc.T("Interiors (schools, hospitals, Nowhere...) get a new room layout. Every room stays reachable; story doors are untouched."),
                current.Rooms);
            _items = Check(Loc.T("Items"),
                Loc.T("Health and ammo pickups trade places with each other, each of a random type of its kind (see Item types)."),
                current.Items);
            _keys = Check(Loc.T("Key items"),
                Loc.T("Key items move to another item's place in their own part of the building, so each is reachable exactly when the original was."),
                current.Keys);
            _enemies = Check(Loc.T("Enemies"),
                Loc.T("Each regular monster spawn becomes a random pick for the area (see Enemies for what goes where). Needs global_chara_pool = 1 (the default)."),
                current.Enemies);
            _starterWeapons = Check(Loc.T("Random starting gun"),
                Loc.T("Start with the Handgun, Shotgun or Hunting Rifle (fixed by the seed). Without the Handgun, it lies somewhere in the world. Set it before a new game."),
                current.StarterWeapons);
            _weapons = Check(Loc.T("Weapons"),
                Loc.T("Every weapon found in the world (melee and guns) lies at a random health/ammo spot anywhere in the game."),
                current.Weapons);
            _unlockables = Check(Loc.T("Unlockables"),
                Loc.T("The bonus items (Katana, Channeling Stone, Chainsaw, Rock Drill) each lie at a random health/ammo spot anywhere in the game, and still need unlocking. The Gasoline Tank is taken out of the game and the Chainsaw and Rock Drill need none (unless both stay home: see Item List)."),
                current.Unlockables);

            var lblSeed = new Label { Text = Loc.T("Seed:"), Left = 12, Top = y + 3, AutoSize = true };
            _seed = new NumericUpDown
            {
                Left = 110, Top = y, Width = 130,
                Minimum = 0, Maximum = RemixSettings.SeedMax, Value = current.Seed,
                BackColor = Field, ForeColor = Color.Gainsboro
            };
            var btnRandom = new Button { Text = Loc.T("Random"), Left = 248, Top = y - 1, Width = 80, Height = 24 };
            btnRandom.Click += (s, e) =>
            {
                var b = new byte[4];
                using (var rng = System.Security.Cryptography.RandomNumberGenerator.Create())
                    rng.GetBytes(b);
                _seed.Value = 1 + BitConverter.ToUInt32(b, 0) % RemixSettings.SeedMax;
            };
            y += 26;
            var lblSeedHint = new Label
            {
                Text = Loc.T("Up to 8 digits (0 to 99999999). The same seed always builds the same layout, items and enemies."),
                Left = 30, Top = y, Width = 690, Height = 18, ForeColor = Color.Gray
            };
            y += 26;

            var lblMaps = new Label { Text = Loc.T("Only these maps:"), Left = 12, Top = y + 3, AutoSize = true };
            _maps = new TextBox
            {
                Left = 110, Top = y, Width = 348, Text = current.Maps ?? "",
                BackColor = Field, ForeColor = Color.Gainsboro
            };
            y += 26;
            var lblMapsHint = new Label
            {
                Text = Loc.T("Comma-separated, e.g. map1_s01,map1_s02. Leave empty for every map."),
                Left = 30, Top = y, Width = 690, Height = 18, ForeColor = Color.Gray
            };
            y += 24;

            var lblRando = new Label
            {
                Text = Loc.T("Randomizer is on: the game ignores remix until it's turned off."),
                Left = 12, Top = y, Width = 700, Height = 18, ForeColor = Warn,
                Visible = randomizerOn
            };
            tabGeneral.Controls.AddRange(new Control[] { lblSeed, _seed, btnRandom, lblSeedHint, lblMaps, _maps, lblMapsHint, lblRando });

            /* ---- Item types: two groups of three linked sliders ---- */
            y = 10;
            tabItems.Controls.Add(new Label
            {
                Text = Loc.T("With Items on, each health spot holds a health item and each ammo spot ammo, of a type picked by these chances. Moving one slider shares what is left between the other two, so each group always adds up to 100%."),
                Left = 12, Top = y, Width = 710, Height = 34, ForeColor = Color.Gray
            });
            y += 44;
            void Group(string title, int first)
            {
                tabItems.Controls.Add(new Label { Text = title, Left = 12, Top = y, AutoSize = true, Font = new Font(Font, FontStyle.Bold) });
                y += 24;
                for (int i = first; i < first + 3; i++)
                {
                    int idx = i;
                    tabItems.Controls.Add(new Label { Text = Loc.T(RemixSettings.WeightNames[i]), Left = 30, Top = y + 6, Width = 130 });
                    _weight[i] = new TrackBar
                    {
                        Left = 165, Top = y, Width = 460, Minimum = 0, Maximum = 100, TickFrequency = 10,
                        SmallChange = 1, LargeChange = 5, Value = current.Weights[i], BackColor = Back
                    };
                    _weightPct[i] = new Label { Left = 635, Top = y + 6, Width = 60, Text = current.Weights[i] + "%" };
                    _weight[i].ValueChanged += (s, e) => Balance(idx);
                    tabItems.Controls.Add(_weight[i]);
                    tabItems.Controls.Add(_weightPct[i]);
                    y += 46;
                }
                y += 10;
            }
            Group(Loc.T("Healing items"), 0);
            Group(Loc.T("Ammo"), 3);
            tabItems.Controls.Add(new Label
            {
                Text = Loc.T("Defaults are the game's own mix: Health Drink 69%, First Aid Kit 27%, Ampoule 4%; handgun bullets 67%, shotgun shells 19%, rifle shells 14%."),
                Left = 12, Top = y, Width = 710, Height = 34, ForeColor = Color.Gray
            });
            for (int g = 0; g < 6; g += 3)
                if (current.Weights[g] + current.Weights[g + 1] + current.Weights[g + 2] != 100)
                    Balance(g);

            /* ---- Enemies: a monster x kind-of-area grid ---- */
            const int nameW = 170, colW = 58, gridLeft = 10;
            y = 8;
            tabEnemies.Controls.Add(new Label
            {
                Text = Loc.T("With Enemies on, a regular spawn in each kind of area becomes one of the monsters ticked in its column. Bosses and scripted spawns stay. A column with nothing ticked keeps its vanilla monsters."),
                Left = 12, Top = y, Width = 710, Height = 34, ForeColor = Color.Gray
            });
            y += 40;
            for (int z = 0; z < RemixSettings.ZoneKeys.Length; z++)
            {
                var head = new Label
                {
                    Text = Loc.T(RemixSettings.ZoneNames[z]) + (z == RemixSettings.ZoneSewers ? " ⚠" : ""),
                    Left = gridLeft + nameW + z * colW - 6, Top = y, Width = colW + 4, Height = 30,
                    TextAlign = ContentAlignment.BottomCenter, Font = new Font(Font.FontFamily, 7.5f),
                    ForeColor = z == RemixSettings.ZoneSewers ? Warn : Color.Gainsboro
                };
                if (z == RemixSettings.ZoneSewers)
                    _tips.SetToolTip(head, Loc.T("Swapping out the sewers' Hanged Scratchers may break the area."));
                tabEnemies.Controls.Add(head);
            }
            y += 34;
            for (int t = 0; t < RemixSettings.EnemyKeys.Length; t++)
            {
                string key  = RemixSettings.EnemyKeys[t];
                string warn = (key == "AirScreamer" || key == "NightFlutter" || key == "Romper")
                                  ? Loc.T("Works best outside.")
                                  : key == "HangedScratcher" ? Loc.T("Works best in the sewers only.") : null;
                var name = new Label
                {
                    Text = Loc.T(RemixSettings.EnemyNames[t]) + (warn != null ? " ⚠" : ""),
                    Left = gridLeft, Top = y + 2, Width = nameW, ForeColor = warn != null ? Warn : Color.Gainsboro
                };
                if (warn != null)
                    _tips.SetToolTip(name, warn);
                tabEnemies.Controls.Add(name);
                for (int z = 0; z < RemixSettings.ZoneKeys.Length; z++)
                {
                    var set = RemixSettings.PoolSet(z, current.Pools[z]);
                    var c   = new CheckBox
                    {
                        Left = gridLeft + nameW + z * colW + colW / 2 - 8, Top = y, Width = 18, Height = 20,
                        Checked = set.Contains(key)
                    };
                    if (warn != null)
                        _tips.SetToolTip(c, warn);
                    _pool[t, z] = c;
                    tabEnemies.Controls.Add(c);
                }
                y += 22;
            }
            y += 8;
            _sewerScratchers = new CheckBox
            {
                Text = Loc.T("Sewers: also replace the sewers' own Hanged Scratchers"),
                Left = 12, Top = y, AutoSize = true, Checked = current.SewerScratchers, ForeColor = Warn
            };
            tabEnemies.Controls.Add(_sewerScratchers);
            y += 22;
            tabEnemies.Controls.Add(new Label
            {
                Text = "⚠ " + Loc.T("Sewers: swapping out the Hanged Scratchers may break the area.") + "\n" +
                       "⚠ " + Loc.T("Air Screamers, Night Flutters and Rompers work best outside. Hanged Scratchers work best in the sewers only."),
                Left = 12, Top = y, Width = 720, Height = 36, ForeColor = Warn
            });

            /* ---- Item List: one tick per item, by section ---- */
            y = 8;
            tabMove.Controls.Add(new Label
            {
                Text = Loc.T("Ticked items move (each with its toggle on the General tab). An unticked one stays at its normal spot, and everything else moves around it as it would anyway. Items from other areas never land on its spot."),
                Left = 12, Top = y, Width = 710, Height = 34, ForeColor = Color.Gray
            });
            y += 40;
            void Section(string title, string[][] list)
            {
                tabMove.Controls.Add(new Label { Text = title, Left = 12, Top = y, AutoSize = true, Font = new Font(Font, FontStyle.Bold) });
                var all  = new LinkLabel { Text = Loc.T("All"),  Left = 230, Top = y, AutoSize = true, LinkColor = Color.SkyBlue };
                var none = new LinkLabel { Text = Loc.T("None"), Left = 270, Top = y, AutoSize = true, LinkColor = Color.SkyBlue };
                var boxes = new List<CheckBox>();
                all.LinkClicked  += (s, e) => boxes.ForEach(b => b.Checked = true);
                none.LinkClicked += (s, e) => boxes.ForEach(b => b.Checked = false);
                tabMove.Controls.Add(all);
                tabMove.Controls.Add(none);
                y += 22;
                for (int i = 0; i < list.Length; i++)
                {
                    var c = new CheckBox
                    {
                        Text = Loc.T(list[i][1]), Left = 30 + (i % 3) * 235, Top = y + (i / 3) * 21, Width = 230, Height = 20,
                        Checked = !current.Fixed.Contains(list[i][0])
                    };
                    _move[list[i][0]] = c;
                    boxes.Add(c);
                    tabMove.Controls.Add(c);
                }
                y += ((list.Length + 2) / 3) * 21 + 12;
            }
            Section(Loc.T("Weapons"), RemixSettings.WeaponItems);
            Section(Loc.T("Key items"), RemixSettings.KeyItems);
            Section(Loc.T("Unlockables"), RemixSettings.UnlockableItems);
            tabMove.Controls.Add(new Label
            {
                Text = Loc.T("With both the Chainsaw and Rock Drill unticked, the Gasoline Tank stays in the game and they need it as in the original. The Kitchen Knife, Flashlight and Pocket Radio, and the hospital's Plastic Bottle and Basement Key, always stay where they are."),
                Left = 12, Top = y, Width = 710, Height = 34, ForeColor = Color.Gray
            });

            /* ---- buttons ---- */
            var btnReset  = new Button { Text = Loc.T("Reset"),  Width = 80, Height = 26, Top = ClientSize.Height - 38, Left = 12 };
            var btnPlay   = new Button { Text = Loc.T("Save && Play"), Width = 100, Height = 26, Top = ClientSize.Height - 38,
                                         Enabled = canPlay };
            var btnOk     = new Button { Text = Loc.T("Save"),   Width = 80, Height = 26, Top = ClientSize.Height - 38 };
            var btnCancel = new Button { Text = Loc.T("Close"),  Width = 80, Height = 26, Top = ClientSize.Height - 38,
                                         DialogResult = DialogResult.Cancel };
            foreach (var b in new[] { btnPlay, btnOk, btnCancel, btnRandom, btnReset })
                b.Width = Math.Max(b.Width, TextRenderer.MeasureText(b.Text, Font).Width + 16);
            btnCancel.Left = ClientSize.Width - 12 - btnCancel.Width;
            btnOk.Left     = btnCancel.Left - 6 - btnOk.Width;
            btnPlay.Left   = btnOk.Left - 6 - btnPlay.Width;
            _tips.SetToolTip(btnPlay, canPlay ? Loc.T("Save these settings to remix.cfg and start the game.")
                                              : Loc.T("SilentHillPC.exe isn't next to this program."));
            _tips.SetToolTip(btnOk, Loc.T("Save these settings to remix.cfg and close."));
            _tips.SetToolTip(btnReset, Loc.T("Every remix setting back to its default (the seed and map list stay)."));

            btnReset.Click += (s, e) =>
            {
                var d = new RemixSettings();
                _rooms.Checked          = d.Rooms;
                _items.Checked          = d.Items;
                _keys.Checked           = d.Keys;
                _unlockables.Checked    = d.Unlockables;
                foreach (var c in _move.Values)
                    c.Checked = true;
                _enemies.Checked        = d.Enemies;
                _starterWeapons.Checked = d.StarterWeapons;
                _weapons.Checked        = d.Weapons;
                _balancing = true;
                for (int i = 0; i < 6; i++)
                {
                    _weight[i].Value     = RemixSettings.DefaultWeights[i];
                    _weightPct[i].Text   = RemixSettings.DefaultWeights[i] + "%";
                }
                _balancing = false;
                for (int t = 0; t < RemixSettings.EnemyKeys.Length; t++)
                    for (int z = 0; z < RemixSettings.ZoneKeys.Length; z++)
                        _pool[t, z].Checked = RemixSettings.DefaultPools[z].Contains(RemixSettings.EnemyKeys[t]);
                _sewerScratchers.Checked = false;
            };

            btnPlay.Click += (s, e) =>
            {
                PlayRequested = true;
                btnOk.PerformClick();
            };

            btnOk.Click += (s, e) =>
            {
                var r = new RemixSettings
                {
                    Rooms   = _rooms.Checked,
                    Items   = _items.Checked,
                    Keys    = _keys.Checked,
                    Enemies = _enemies.Checked,
                    Unlockables    = _unlockables.Checked,
                    StarterWeapons = _starterWeapons.Checked,
                    Weapons        = _weapons.Checked,
                    Seed    = (uint)_seed.Value,
                    Maps    = _maps.Text.Replace(" ", "").Trim(','),
                    SewerScratchers = _sewerScratchers.Checked
                };
                for (int i = 0; i < 6; i++)
                    r.Weights[i] = _weight[i].Value;
                foreach (var kv in _move)
                    if (!kv.Value.Checked)
                        r.Fixed.Add(kv.Key);
                for (int z = 0; z < RemixSettings.ZoneKeys.Length; z++)
                {
                    var set = new HashSet<string>();
                    for (int t = 0; t < RemixSettings.EnemyKeys.Length; t++)
                        if (_pool[t, z].Checked)
                            set.Add(RemixSettings.EnemyKeys[t]);
                    r.Pools[z] = RemixSettings.PoolValue(z, set);
                }
                Result       = r;
                DialogResult = DialogResult.OK;
                Close();
            };

            Controls.Add(btnReset);
            Controls.Add(btnPlay);
            Controls.Add(btnOk);
            Controls.Add(btnCancel);
            AcceptButton = btnOk;
            CancelButton = btnCancel;
        }

        /// <summary>Slider `moved` keeps its value; the other two of its group share
        /// what is left in their current proportion (evenly when both are 0).</summary>
        private void Balance(int moved)
        {
            if (_balancing || _weight[moved] == null)
                return;
            int first = moved < 3 ? 0 : 3, a = -1, b = -1;
            for (int i = first; i < first + 3; i++)
            {
                if (_weight[i] == null)
                    return;
                if (i == moved) continue;
                if (a < 0) a = i; else b = i;
            }
            _balancing = true;
            int rest = 100 - _weight[moved].Value, sum = _weight[a].Value + _weight[b].Value;
            int va   = sum == 0 ? (rest + 1) / 2 : (int)Math.Round(rest * (double)_weight[a].Value / sum);
            va = Math.Max(0, Math.Min(rest, va));
            _weight[a].Value = va;
            _weight[b].Value = rest - va;
            for (int i = first; i < first + 3; i++)
                _weightPct[i].Text = _weight[i].Value + "%";
            _balancing = false;
        }
    }
}
