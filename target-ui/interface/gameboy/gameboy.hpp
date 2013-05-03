struct InterfaceGameBoy : InterfaceCore, GameBoy::Interface {
  void initialize();

  bool cartridgeLoaded();
  bool loadCartridge(GameBoy::System::Revision revision, const string &filename);
  void unloadCartridge();

  void power();
  void reset();
  void run();

  serializer serialize();
  bool unserialize(serializer&);

  void setCheats(const lstring &list = lstring{});

  void videoRefresh(const uint16_t *data);
  void audioSample(int16_t csample, int16_t lsample, int16_t rsample);
  bool inputPoll(unsigned id);
};
