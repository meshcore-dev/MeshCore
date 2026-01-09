package com.meshcore.team.data.database;

import androidx.annotation.NonNull;
import androidx.room.DatabaseConfiguration;
import androidx.room.InvalidationTracker;
import androidx.room.RoomDatabase;
import androidx.room.RoomOpenHelper;
import androidx.room.migration.AutoMigrationSpec;
import androidx.room.migration.Migration;
import androidx.room.util.DBUtil;
import androidx.room.util.TableInfo;
import androidx.sqlite.db.SupportSQLiteDatabase;
import androidx.sqlite.db.SupportSQLiteOpenHelper;
import java.lang.Class;
import java.lang.Override;
import java.lang.String;
import java.lang.SuppressWarnings;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import javax.annotation.processing.Generated;

@Generated("androidx.room.RoomProcessor")
@SuppressWarnings({"unchecked", "deprecation"})
public final class AppDatabase_Impl extends AppDatabase {
  private volatile MessageDao _messageDao;

  private volatile NodeDao _nodeDao;

  private volatile ChannelDao _channelDao;

  private volatile AckRecordDao _ackRecordDao;

  @Override
  @NonNull
  protected SupportSQLiteOpenHelper createOpenHelper(@NonNull final DatabaseConfiguration config) {
    final SupportSQLiteOpenHelper.Callback _openCallback = new RoomOpenHelper(config, new RoomOpenHelper.Delegate(1) {
      @Override
      public void createAllTables(@NonNull final SupportSQLiteDatabase db) {
        db.execSQL("CREATE TABLE IF NOT EXISTS `messages` (`id` TEXT NOT NULL, `senderId` BLOB NOT NULL, `senderName` TEXT, `channelHash` INTEGER NOT NULL, `content` TEXT NOT NULL, `timestamp` INTEGER NOT NULL, `isPrivate` INTEGER NOT NULL, `ackChecksum` BLOB, `deliveryStatus` TEXT NOT NULL, `heardByCount` INTEGER NOT NULL, `attempt` INTEGER NOT NULL, `isSentByMe` INTEGER NOT NULL, PRIMARY KEY(`id`))");
        db.execSQL("CREATE TABLE IF NOT EXISTS `nodes` (`publicKey` BLOB NOT NULL, `hash` INTEGER NOT NULL, `name` TEXT, `latitude` REAL, `longitude` REAL, `lastSeen` INTEGER NOT NULL, `batteryMilliVolts` INTEGER, `isRepeater` INTEGER NOT NULL, `isRoomServer` INTEGER NOT NULL, `isDirect` INTEGER NOT NULL, PRIMARY KEY(`publicKey`))");
        db.execSQL("CREATE TABLE IF NOT EXISTS `channels` (`hash` INTEGER NOT NULL, `name` TEXT NOT NULL, `sharedKey` BLOB NOT NULL, `isPublic` INTEGER NOT NULL, `shareLocation` INTEGER NOT NULL, `createdAt` INTEGER NOT NULL, PRIMARY KEY(`hash`))");
        db.execSQL("CREATE TABLE IF NOT EXISTS `ack_records` (`id` INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL, `messageId` TEXT NOT NULL, `ackChecksum` BLOB NOT NULL, `devicePublicKey` BLOB NOT NULL, `roundTripTimeMs` INTEGER NOT NULL, `isDirect` INTEGER NOT NULL, `receivedAt` INTEGER NOT NULL)");
        db.execSQL("CREATE TABLE IF NOT EXISTS room_master_table (id INTEGER PRIMARY KEY,identity_hash TEXT)");
        db.execSQL("INSERT OR REPLACE INTO room_master_table (id,identity_hash) VALUES(42, '453b8b1fc9d79b280213f13984a36893')");
      }

      @Override
      public void dropAllTables(@NonNull final SupportSQLiteDatabase db) {
        db.execSQL("DROP TABLE IF EXISTS `messages`");
        db.execSQL("DROP TABLE IF EXISTS `nodes`");
        db.execSQL("DROP TABLE IF EXISTS `channels`");
        db.execSQL("DROP TABLE IF EXISTS `ack_records`");
        final List<? extends RoomDatabase.Callback> _callbacks = mCallbacks;
        if (_callbacks != null) {
          for (RoomDatabase.Callback _callback : _callbacks) {
            _callback.onDestructiveMigration(db);
          }
        }
      }

      @Override
      public void onCreate(@NonNull final SupportSQLiteDatabase db) {
        final List<? extends RoomDatabase.Callback> _callbacks = mCallbacks;
        if (_callbacks != null) {
          for (RoomDatabase.Callback _callback : _callbacks) {
            _callback.onCreate(db);
          }
        }
      }

      @Override
      public void onOpen(@NonNull final SupportSQLiteDatabase db) {
        mDatabase = db;
        internalInitInvalidationTracker(db);
        final List<? extends RoomDatabase.Callback> _callbacks = mCallbacks;
        if (_callbacks != null) {
          for (RoomDatabase.Callback _callback : _callbacks) {
            _callback.onOpen(db);
          }
        }
      }

      @Override
      public void onPreMigrate(@NonNull final SupportSQLiteDatabase db) {
        DBUtil.dropFtsSyncTriggers(db);
      }

      @Override
      public void onPostMigrate(@NonNull final SupportSQLiteDatabase db) {
      }

      @Override
      @NonNull
      public RoomOpenHelper.ValidationResult onValidateSchema(
          @NonNull final SupportSQLiteDatabase db) {
        final HashMap<String, TableInfo.Column> _columnsMessages = new HashMap<String, TableInfo.Column>(12);
        _columnsMessages.put("id", new TableInfo.Column("id", "TEXT", true, 1, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("senderId", new TableInfo.Column("senderId", "BLOB", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("senderName", new TableInfo.Column("senderName", "TEXT", false, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("channelHash", new TableInfo.Column("channelHash", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("content", new TableInfo.Column("content", "TEXT", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("timestamp", new TableInfo.Column("timestamp", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("isPrivate", new TableInfo.Column("isPrivate", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("ackChecksum", new TableInfo.Column("ackChecksum", "BLOB", false, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("deliveryStatus", new TableInfo.Column("deliveryStatus", "TEXT", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("heardByCount", new TableInfo.Column("heardByCount", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("attempt", new TableInfo.Column("attempt", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsMessages.put("isSentByMe", new TableInfo.Column("isSentByMe", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        final HashSet<TableInfo.ForeignKey> _foreignKeysMessages = new HashSet<TableInfo.ForeignKey>(0);
        final HashSet<TableInfo.Index> _indicesMessages = new HashSet<TableInfo.Index>(0);
        final TableInfo _infoMessages = new TableInfo("messages", _columnsMessages, _foreignKeysMessages, _indicesMessages);
        final TableInfo _existingMessages = TableInfo.read(db, "messages");
        if (!_infoMessages.equals(_existingMessages)) {
          return new RoomOpenHelper.ValidationResult(false, "messages(com.meshcore.team.data.database.MessageEntity).\n"
                  + " Expected:\n" + _infoMessages + "\n"
                  + " Found:\n" + _existingMessages);
        }
        final HashMap<String, TableInfo.Column> _columnsNodes = new HashMap<String, TableInfo.Column>(10);
        _columnsNodes.put("publicKey", new TableInfo.Column("publicKey", "BLOB", true, 1, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsNodes.put("hash", new TableInfo.Column("hash", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsNodes.put("name", new TableInfo.Column("name", "TEXT", false, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsNodes.put("latitude", new TableInfo.Column("latitude", "REAL", false, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsNodes.put("longitude", new TableInfo.Column("longitude", "REAL", false, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsNodes.put("lastSeen", new TableInfo.Column("lastSeen", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsNodes.put("batteryMilliVolts", new TableInfo.Column("batteryMilliVolts", "INTEGER", false, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsNodes.put("isRepeater", new TableInfo.Column("isRepeater", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsNodes.put("isRoomServer", new TableInfo.Column("isRoomServer", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsNodes.put("isDirect", new TableInfo.Column("isDirect", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        final HashSet<TableInfo.ForeignKey> _foreignKeysNodes = new HashSet<TableInfo.ForeignKey>(0);
        final HashSet<TableInfo.Index> _indicesNodes = new HashSet<TableInfo.Index>(0);
        final TableInfo _infoNodes = new TableInfo("nodes", _columnsNodes, _foreignKeysNodes, _indicesNodes);
        final TableInfo _existingNodes = TableInfo.read(db, "nodes");
        if (!_infoNodes.equals(_existingNodes)) {
          return new RoomOpenHelper.ValidationResult(false, "nodes(com.meshcore.team.data.database.NodeEntity).\n"
                  + " Expected:\n" + _infoNodes + "\n"
                  + " Found:\n" + _existingNodes);
        }
        final HashMap<String, TableInfo.Column> _columnsChannels = new HashMap<String, TableInfo.Column>(6);
        _columnsChannels.put("hash", new TableInfo.Column("hash", "INTEGER", true, 1, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsChannels.put("name", new TableInfo.Column("name", "TEXT", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsChannels.put("sharedKey", new TableInfo.Column("sharedKey", "BLOB", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsChannels.put("isPublic", new TableInfo.Column("isPublic", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsChannels.put("shareLocation", new TableInfo.Column("shareLocation", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsChannels.put("createdAt", new TableInfo.Column("createdAt", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        final HashSet<TableInfo.ForeignKey> _foreignKeysChannels = new HashSet<TableInfo.ForeignKey>(0);
        final HashSet<TableInfo.Index> _indicesChannels = new HashSet<TableInfo.Index>(0);
        final TableInfo _infoChannels = new TableInfo("channels", _columnsChannels, _foreignKeysChannels, _indicesChannels);
        final TableInfo _existingChannels = TableInfo.read(db, "channels");
        if (!_infoChannels.equals(_existingChannels)) {
          return new RoomOpenHelper.ValidationResult(false, "channels(com.meshcore.team.data.database.ChannelEntity).\n"
                  + " Expected:\n" + _infoChannels + "\n"
                  + " Found:\n" + _existingChannels);
        }
        final HashMap<String, TableInfo.Column> _columnsAckRecords = new HashMap<String, TableInfo.Column>(7);
        _columnsAckRecords.put("id", new TableInfo.Column("id", "INTEGER", true, 1, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsAckRecords.put("messageId", new TableInfo.Column("messageId", "TEXT", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsAckRecords.put("ackChecksum", new TableInfo.Column("ackChecksum", "BLOB", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsAckRecords.put("devicePublicKey", new TableInfo.Column("devicePublicKey", "BLOB", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsAckRecords.put("roundTripTimeMs", new TableInfo.Column("roundTripTimeMs", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsAckRecords.put("isDirect", new TableInfo.Column("isDirect", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        _columnsAckRecords.put("receivedAt", new TableInfo.Column("receivedAt", "INTEGER", true, 0, null, TableInfo.CREATED_FROM_ENTITY));
        final HashSet<TableInfo.ForeignKey> _foreignKeysAckRecords = new HashSet<TableInfo.ForeignKey>(0);
        final HashSet<TableInfo.Index> _indicesAckRecords = new HashSet<TableInfo.Index>(0);
        final TableInfo _infoAckRecords = new TableInfo("ack_records", _columnsAckRecords, _foreignKeysAckRecords, _indicesAckRecords);
        final TableInfo _existingAckRecords = TableInfo.read(db, "ack_records");
        if (!_infoAckRecords.equals(_existingAckRecords)) {
          return new RoomOpenHelper.ValidationResult(false, "ack_records(com.meshcore.team.data.database.AckRecordEntity).\n"
                  + " Expected:\n" + _infoAckRecords + "\n"
                  + " Found:\n" + _existingAckRecords);
        }
        return new RoomOpenHelper.ValidationResult(true, null);
      }
    }, "453b8b1fc9d79b280213f13984a36893", "de920fbc3df5e783ebe492b3171b6237");
    final SupportSQLiteOpenHelper.Configuration _sqliteConfig = SupportSQLiteOpenHelper.Configuration.builder(config.context).name(config.name).callback(_openCallback).build();
    final SupportSQLiteOpenHelper _helper = config.sqliteOpenHelperFactory.create(_sqliteConfig);
    return _helper;
  }

  @Override
  @NonNull
  protected InvalidationTracker createInvalidationTracker() {
    final HashMap<String, String> _shadowTablesMap = new HashMap<String, String>(0);
    final HashMap<String, Set<String>> _viewTables = new HashMap<String, Set<String>>(0);
    return new InvalidationTracker(this, _shadowTablesMap, _viewTables, "messages","nodes","channels","ack_records");
  }

  @Override
  public void clearAllTables() {
    super.assertNotMainThread();
    final SupportSQLiteDatabase _db = super.getOpenHelper().getWritableDatabase();
    try {
      super.beginTransaction();
      _db.execSQL("DELETE FROM `messages`");
      _db.execSQL("DELETE FROM `nodes`");
      _db.execSQL("DELETE FROM `channels`");
      _db.execSQL("DELETE FROM `ack_records`");
      super.setTransactionSuccessful();
    } finally {
      super.endTransaction();
      _db.query("PRAGMA wal_checkpoint(FULL)").close();
      if (!_db.inTransaction()) {
        _db.execSQL("VACUUM");
      }
    }
  }

  @Override
  @NonNull
  protected Map<Class<?>, List<Class<?>>> getRequiredTypeConverters() {
    final HashMap<Class<?>, List<Class<?>>> _typeConvertersMap = new HashMap<Class<?>, List<Class<?>>>();
    _typeConvertersMap.put(MessageDao.class, MessageDao_Impl.getRequiredConverters());
    _typeConvertersMap.put(NodeDao.class, NodeDao_Impl.getRequiredConverters());
    _typeConvertersMap.put(ChannelDao.class, ChannelDao_Impl.getRequiredConverters());
    _typeConvertersMap.put(AckRecordDao.class, AckRecordDao_Impl.getRequiredConverters());
    return _typeConvertersMap;
  }

  @Override
  @NonNull
  public Set<Class<? extends AutoMigrationSpec>> getRequiredAutoMigrationSpecs() {
    final HashSet<Class<? extends AutoMigrationSpec>> _autoMigrationSpecsSet = new HashSet<Class<? extends AutoMigrationSpec>>();
    return _autoMigrationSpecsSet;
  }

  @Override
  @NonNull
  public List<Migration> getAutoMigrations(
      @NonNull final Map<Class<? extends AutoMigrationSpec>, AutoMigrationSpec> autoMigrationSpecs) {
    final List<Migration> _autoMigrations = new ArrayList<Migration>();
    return _autoMigrations;
  }

  @Override
  public MessageDao messageDao() {
    if (_messageDao != null) {
      return _messageDao;
    } else {
      synchronized(this) {
        if(_messageDao == null) {
          _messageDao = new MessageDao_Impl(this);
        }
        return _messageDao;
      }
    }
  }

  @Override
  public NodeDao nodeDao() {
    if (_nodeDao != null) {
      return _nodeDao;
    } else {
      synchronized(this) {
        if(_nodeDao == null) {
          _nodeDao = new NodeDao_Impl(this);
        }
        return _nodeDao;
      }
    }
  }

  @Override
  public ChannelDao channelDao() {
    if (_channelDao != null) {
      return _channelDao;
    } else {
      synchronized(this) {
        if(_channelDao == null) {
          _channelDao = new ChannelDao_Impl(this);
        }
        return _channelDao;
      }
    }
  }

  @Override
  public AckRecordDao ackRecordDao() {
    if (_ackRecordDao != null) {
      return _ackRecordDao;
    } else {
      synchronized(this) {
        if(_ackRecordDao == null) {
          _ackRecordDao = new AckRecordDao_Impl(this);
        }
        return _ackRecordDao;
      }
    }
  }
}
