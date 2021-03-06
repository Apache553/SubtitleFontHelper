﻿using libSubtitleFontHelper;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.Xml;
using System.Xml.Serialization;


namespace SubtitleFontHelper
{
    /// <summary>
    /// MainWindow.xaml 的交互逻辑
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
        }

        public static readonly DependencyProperty LogTextBoxKeepBottomProperty =
            DependencyProperty.Register("LogTextBoxKeepBottom", typeof(bool), typeof(MainWindow),
            new FrameworkPropertyMetadata((bool)true, new PropertyChangedCallback(LogTextBoxKeepBottom_Changed)));

        public bool LogTextBoxKeepBottom
        {
            get { return (bool)GetValue(LogTextBoxKeepBottomProperty); }
            set { SetValue(LogTextBoxKeepBottomProperty, value); }
        }

        /*
        FontFileCollection collection = null;
        private void Button_Click(object sender, RoutedEventArgs e)
        {
            btnScan.IsEnabled = false;

            new Thread(delegate ()
              {
                  FontScanner scanner = new FontScanner();
                  List<string> files = FontScanner.FilterDirectory(
                      @"E:\超级字体整合包 XZ\",
                      new string[] { ".ttf", ".ttc", ".otf", ".otc" }
                      );

                  scanner.ScanFontAsync(files, 
                  delegate (string file, int index, Exception exp)
                  {
                      MessageBox.Show(String.Format("Error processing face {0} of {1}: {2}", file, index, exp.Message),
                          "Error", MessageBoxButton.OK);
                  },
                  delegate (FontFileCollection o)
                  {
                      collection = o;
                  });

                  do
                  {
                      Tuple<string, int> status_tuple = scanner.GetStatusTuple();
                      Thread.Sleep(50);
                      btnScan.Dispatcher.Invoke(delegate ()
                      {
                          InfoL.Content = status_tuple.Item1;
                          pbarScan.Value = (float)status_tuple.Item2 / files.Count * pbarScan.Maximum;
                      });
                      if (status_tuple.Item2 >= files.Count) break;
                  } while (true);

                  btnScan.Dispatcher.Invoke(delegate ()
                  {
                      btnScan.IsEnabled = true;
                  });

                  collection.WriteToXml(@"E:\myfest.xml");
              }).Start();
        }

        private void btnCheck_Click(object sender, RoutedEventArgs e)
        {
#if true
            FontFileCollection coll = FontFileCollection.ReadFromXml(@"E:\myfest.xml");
            FontIndex gindex = GlobalContext.Instance.FontIndex;
            foreach(var file in coll.FontFile)
            {
                gindex.AddFont(file);
            }

            GlobalContext.Instance.NamedPipeServer = new NamedPipeServer("SubtitleFontHelperPipe");

            GlobalContext.Instance.ServerThread = new Thread(delegate ()
              {
                  GlobalContext.Instance.NamedPipeServer.RunServer();
              });

            GlobalContext.Instance.ServerThread.Start();

#else

            
            FreeTypeFontMetadata fontMetadata = new FreeTypeFontMetadata();
            fontMetadata.OpenFontFile(@"E:\超级字体整合包 XZ\Legacy\字体\超级字体整合包X\苏新诗古印宋简.ttf");
            IFreeTypeFontFaceMetadata fontFaceMetadata = fontMetadata.GetFontFace(0);
            FontFace face = FontFace.FromMetadata(fontFaceMetadata);
#endif
        }
        */
        void MainWindow_Closing(object sender, CancelEventArgs e)
        {
            if (GlobalContext.NamedPipeServer != null)
            {
                GlobalContext.NamedPipeServer.Cancel();
            }

        }

        private void OnExit(object sender, ExecutedRoutedEventArgs e)
        {
            Application.Current.Shutdown();

        }

        private void ClearButton_Click(object sender, RoutedEventArgs e)
        {
            LogTextBox.Text = "";
        }

        private static void LogTextBoxKeepBottom_Changed(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            if ((bool)e.NewValue == true)
            {
                MainWindow window = d as MainWindow;
                window.LogTextBox.ScrollToEnd();
            }
        }

        private void LogTextBox_TextChanged(object sender, TextChangedEventArgs e)
        {
            if (LogTextBoxKeepBottom)
            {
                LogTextBox.ScrollToEnd();
            }
        }

        private void LogTextBox_MouseWheel(object sender, MouseWheelEventArgs e)
        {
            LogTextBoxKeepBottom = false;
        }

        private void LogTextBox_PreviewMouseDown(object sender, MouseButtonEventArgs e)
        {
            LogTextBoxKeepBottom = false;
        }

        private void ShowWiz_OnClick(object sender, RoutedEventArgs e)
        {
            IndexBuilder indexBuilder = new IndexBuilder();
            indexBuilder.ShowDialog();
        }

        private void TestPipe_OnClick(object sender, RoutedEventArgs e)
        {
            FontMatcher fontMatcher = new FontMatcher();
            using (Stream fs = new FileStream(@"E:\超级字体整合包 XZ\new_index.xml", FileMode.Open))
            {
                FontFaceCollection faceCollection = FontFaceCollection.ParseStream(fs);

                foreach (var face in faceCollection.FontFace)
                {
                    fontMatcher.AddFontFace(face);
                }
            }

            GlobalContext.FontMatcher = fontMatcher;

            GlobalContext.NamedPipeServer = new NamedPipeServer(@"SubtitleFontHelperPipe");
            GlobalContext.ServerThread = new Thread(delegate ()
            {
                GlobalContext.NamedPipeServer.RunServer();
            });
            GlobalContext.ServerThread.Start();
        }

        private void BuildIndex_OnClick(object sender, RoutedEventArgs e)
        {
            DirectoryInfo dirInfo = new DirectoryInfo(@"E:\超级字体整合包 XZ\Legacy");
            RecursiveFileEnumerator rfe = new RecursiveFileEnumerator(new DirectoryInfo[] { dirInfo });
            List<string> extList = new string[] { ".otf", ".ttf", ".ttc", ".otc" }.ToList();
            FontFaceCollection faceCollection = new FontFaceCollection();

            using (FileStream stream = new FileStream(@"E:\超级字体整合包 XZ\new_index.xml", FileMode.Create)) 
            {
                void Proc()
                {
                    using (FTContext context = new FTContext())
                    {
                        using (FTFontFileMap fileMap = new FTFontFileMap())
                        {
                            FTFontReader reader = new FTFontReader(context, fileMap);
                            FileInfo fileInfo = rfe.NextFileExtLocked(extList);
                            while (fileInfo != null)
                            {
                                bool success = fileMap.OpenFile(fileInfo.FullName);
                                if (success)
                                {
                                    int n = reader.GetFontFaceCount();
                                    for (int i = 0; i < n; ++i)
                                    {
                                        FTFontFaceInfo info = reader.GetFaceInfo(i);
                                        FontFaceInfo faceInfo = new FontFaceInfo();
                                        faceInfo.FullName = info.FullName;
                                        faceInfo.PostScriptName = info.PostScriptName;
                                        faceInfo.Win32FamilyName = info.Win32FamilyName;
                                        //faceInfo.FamilyName = info.TypographicFamilyName;
                                        faceInfo.FileName = fileInfo.FullName;
                                        faceInfo.Index = i;
                                        lock (faceCollection) faceCollection.FontFace.Add(faceInfo);
                                    }
                                }

                                fileInfo = rfe.NextFileExtLocked(extList);
                            }
                        }
                    }
                }
                int threadMax = 4;
                Thread[] threads = new Thread[threadMax];
                for (int i = 0; i < threads.Length; ++i)
                {
                    threads[i] = new Thread(Proc);
                    threads[i].Start();
                }
                for (int i = 0; i < threads.Length; ++i)
                {
                    threads[i].Join();
                }

                faceCollection.WriteStream(stream);
            }

        }

        private void TestFont_OnClick(object sender, RoutedEventArgs e)
        {
            using (FTContext context = new FTContext())
            {
                using (FTFontFileMap fileMap = new FTFontFileMap())
                {
                    FTFontReader reader = new FTFontReader(context, fileMap);
                    bool success = fileMap.OpenFile(@"E:\超级字体整合包 XZ\完整包\Microsoft（微软）\繁体\微软繁标宋.TTF");
                    if (success)
                    {
                        int n = reader.GetFontFaceCount();
                        for (int i = 0; i < n; ++i)
                        {
                            FTFontFaceInfo info = reader.GetFaceInfo(i);
                        }
                    }
                }
            }
        }
    }
}
