﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Documents.DocumentStructures;
using System.Xml.Serialization;

namespace SubtitleFontHelper
{
    [Serializable]
    public class FontFamilyInfo
    {
        /// <summary>
        /// The typographical family name
        /// </summary>
        [XmlElement("FamilyName")] public List<string> FamilyName { get; set; } = new List<string>();

        /// <summary>
        /// Win32 GDI Compatible family name
        /// </summary>
        [XmlElement("Win32FamilyName")] public List<string> Win32FamilyName { get; set; } = new List<string>();

        /// <summary>
        /// The font faces in the family
        /// </summary>
        [XmlElement("FontFace")] public List<FontFaceInfo> FontFace { get; set; } = new List<FontFaceInfo>();
    }

    public class FontMappingRecord
    {
        public string MapName { get; set; }
        public List<FontFaceInfo> FontFace { get; set; } = new List<FontFaceInfo>();
    }
}
