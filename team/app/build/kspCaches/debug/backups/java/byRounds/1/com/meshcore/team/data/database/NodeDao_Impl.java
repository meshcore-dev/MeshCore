package com.meshcore.team.data.database;

import android.database.Cursor;
import android.os.CancellationSignal;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.room.CoroutinesRoom;
import androidx.room.EntityDeletionOrUpdateAdapter;
import androidx.room.EntityInsertionAdapter;
import androidx.room.RoomDatabase;
import androidx.room.RoomSQLiteQuery;
import androidx.room.SharedSQLiteStatement;
import androidx.room.util.CursorUtil;
import androidx.room.util.DBUtil;
import androidx.sqlite.db.SupportSQLiteStatement;
import java.lang.Class;
import java.lang.Double;
import java.lang.Exception;
import java.lang.Integer;
import java.lang.Object;
import java.lang.Override;
import java.lang.String;
import java.lang.SuppressWarnings;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.Callable;
import javax.annotation.processing.Generated;
import kotlin.Unit;
import kotlin.coroutines.Continuation;
import kotlinx.coroutines.flow.Flow;

@Generated("androidx.room.RoomProcessor")
@SuppressWarnings({"unchecked", "deprecation"})
public final class NodeDao_Impl implements NodeDao {
  private final RoomDatabase __db;

  private final EntityInsertionAdapter<NodeEntity> __insertionAdapterOfNodeEntity;

  private final EntityDeletionOrUpdateAdapter<NodeEntity> __deletionAdapterOfNodeEntity;

  private final EntityDeletionOrUpdateAdapter<NodeEntity> __updateAdapterOfNodeEntity;

  private final SharedSQLiteStatement __preparedStmtOfDeleteAllNodes;

  private final SharedSQLiteStatement __preparedStmtOfUpdateNodeLocation;

  private final SharedSQLiteStatement __preparedStmtOfUpdateNodeLastSeen;

  public NodeDao_Impl(@NonNull final RoomDatabase __db) {
    this.__db = __db;
    this.__insertionAdapterOfNodeEntity = new EntityInsertionAdapter<NodeEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "INSERT OR REPLACE INTO `nodes` (`publicKey`,`hash`,`name`,`latitude`,`longitude`,`lastSeen`,`batteryMilliVolts`,`isRepeater`,`isRoomServer`,`isDirect`,`hopCount`) VALUES (?,?,?,?,?,?,?,?,?,?,?)";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final NodeEntity entity) {
        statement.bindBlob(1, entity.getPublicKey());
        statement.bindLong(2, entity.getHash());
        if (entity.getName() == null) {
          statement.bindNull(3);
        } else {
          statement.bindString(3, entity.getName());
        }
        if (entity.getLatitude() == null) {
          statement.bindNull(4);
        } else {
          statement.bindDouble(4, entity.getLatitude());
        }
        if (entity.getLongitude() == null) {
          statement.bindNull(5);
        } else {
          statement.bindDouble(5, entity.getLongitude());
        }
        statement.bindLong(6, entity.getLastSeen());
        if (entity.getBatteryMilliVolts() == null) {
          statement.bindNull(7);
        } else {
          statement.bindLong(7, entity.getBatteryMilliVolts());
        }
        final int _tmp = entity.isRepeater() ? 1 : 0;
        statement.bindLong(8, _tmp);
        final int _tmp_1 = entity.isRoomServer() ? 1 : 0;
        statement.bindLong(9, _tmp_1);
        final int _tmp_2 = entity.isDirect() ? 1 : 0;
        statement.bindLong(10, _tmp_2);
        statement.bindLong(11, entity.getHopCount());
      }
    };
    this.__deletionAdapterOfNodeEntity = new EntityDeletionOrUpdateAdapter<NodeEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "DELETE FROM `nodes` WHERE `publicKey` = ?";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final NodeEntity entity) {
        statement.bindBlob(1, entity.getPublicKey());
      }
    };
    this.__updateAdapterOfNodeEntity = new EntityDeletionOrUpdateAdapter<NodeEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "UPDATE OR ABORT `nodes` SET `publicKey` = ?,`hash` = ?,`name` = ?,`latitude` = ?,`longitude` = ?,`lastSeen` = ?,`batteryMilliVolts` = ?,`isRepeater` = ?,`isRoomServer` = ?,`isDirect` = ?,`hopCount` = ? WHERE `publicKey` = ?";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final NodeEntity entity) {
        statement.bindBlob(1, entity.getPublicKey());
        statement.bindLong(2, entity.getHash());
        if (entity.getName() == null) {
          statement.bindNull(3);
        } else {
          statement.bindString(3, entity.getName());
        }
        if (entity.getLatitude() == null) {
          statement.bindNull(4);
        } else {
          statement.bindDouble(4, entity.getLatitude());
        }
        if (entity.getLongitude() == null) {
          statement.bindNull(5);
        } else {
          statement.bindDouble(5, entity.getLongitude());
        }
        statement.bindLong(6, entity.getLastSeen());
        if (entity.getBatteryMilliVolts() == null) {
          statement.bindNull(7);
        } else {
          statement.bindLong(7, entity.getBatteryMilliVolts());
        }
        final int _tmp = entity.isRepeater() ? 1 : 0;
        statement.bindLong(8, _tmp);
        final int _tmp_1 = entity.isRoomServer() ? 1 : 0;
        statement.bindLong(9, _tmp_1);
        final int _tmp_2 = entity.isDirect() ? 1 : 0;
        statement.bindLong(10, _tmp_2);
        statement.bindLong(11, entity.getHopCount());
        statement.bindBlob(12, entity.getPublicKey());
      }
    };
    this.__preparedStmtOfDeleteAllNodes = new SharedSQLiteStatement(__db) {
      @Override
      @NonNull
      public String createQuery() {
        final String _query = "DELETE FROM nodes";
        return _query;
      }
    };
    this.__preparedStmtOfUpdateNodeLocation = new SharedSQLiteStatement(__db) {
      @Override
      @NonNull
      public String createQuery() {
        final String _query = "UPDATE nodes SET latitude = ?, longitude = ?, lastSeen = ? WHERE publicKey = ?";
        return _query;
      }
    };
    this.__preparedStmtOfUpdateNodeLastSeen = new SharedSQLiteStatement(__db) {
      @Override
      @NonNull
      public String createQuery() {
        final String _query = "UPDATE nodes SET lastSeen = ?, isDirect = ? WHERE publicKey = ?";
        return _query;
      }
    };
  }

  @Override
  public Object insertNode(final NodeEntity node, final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __insertionAdapterOfNodeEntity.insert(node);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object deleteNode(final NodeEntity node, final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __deletionAdapterOfNodeEntity.handle(node);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object updateNode(final NodeEntity node, final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __updateAdapterOfNodeEntity.handle(node);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object deleteAllNodes(final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        final SupportSQLiteStatement _stmt = __preparedStmtOfDeleteAllNodes.acquire();
        try {
          __db.beginTransaction();
          try {
            _stmt.executeUpdateDelete();
            __db.setTransactionSuccessful();
            return Unit.INSTANCE;
          } finally {
            __db.endTransaction();
          }
        } finally {
          __preparedStmtOfDeleteAllNodes.release(_stmt);
        }
      }
    }, $completion);
  }

  @Override
  public Object updateNodeLocation(final byte[] publicKey, final double latitude,
      final double longitude, final long lastSeen, final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        final SupportSQLiteStatement _stmt = __preparedStmtOfUpdateNodeLocation.acquire();
        int _argIndex = 1;
        _stmt.bindDouble(_argIndex, latitude);
        _argIndex = 2;
        _stmt.bindDouble(_argIndex, longitude);
        _argIndex = 3;
        _stmt.bindLong(_argIndex, lastSeen);
        _argIndex = 4;
        _stmt.bindBlob(_argIndex, publicKey);
        try {
          __db.beginTransaction();
          try {
            _stmt.executeUpdateDelete();
            __db.setTransactionSuccessful();
            return Unit.INSTANCE;
          } finally {
            __db.endTransaction();
          }
        } finally {
          __preparedStmtOfUpdateNodeLocation.release(_stmt);
        }
      }
    }, $completion);
  }

  @Override
  public Object updateNodeLastSeen(final byte[] publicKey, final long lastSeen,
      final boolean isDirect, final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        final SupportSQLiteStatement _stmt = __preparedStmtOfUpdateNodeLastSeen.acquire();
        int _argIndex = 1;
        _stmt.bindLong(_argIndex, lastSeen);
        _argIndex = 2;
        final int _tmp = isDirect ? 1 : 0;
        _stmt.bindLong(_argIndex, _tmp);
        _argIndex = 3;
        _stmt.bindBlob(_argIndex, publicKey);
        try {
          __db.beginTransaction();
          try {
            _stmt.executeUpdateDelete();
            __db.setTransactionSuccessful();
            return Unit.INSTANCE;
          } finally {
            __db.endTransaction();
          }
        } finally {
          __preparedStmtOfUpdateNodeLastSeen.release(_stmt);
        }
      }
    }, $completion);
  }

  @Override
  public Flow<List<NodeEntity>> getAllNodes() {
    final String _sql = "SELECT * FROM nodes ORDER BY lastSeen DESC";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 0);
    return CoroutinesRoom.createFlow(__db, false, new String[] {"nodes"}, new Callable<List<NodeEntity>>() {
      @Override
      @NonNull
      public List<NodeEntity> call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfPublicKey = CursorUtil.getColumnIndexOrThrow(_cursor, "publicKey");
          final int _cursorIndexOfHash = CursorUtil.getColumnIndexOrThrow(_cursor, "hash");
          final int _cursorIndexOfName = CursorUtil.getColumnIndexOrThrow(_cursor, "name");
          final int _cursorIndexOfLatitude = CursorUtil.getColumnIndexOrThrow(_cursor, "latitude");
          final int _cursorIndexOfLongitude = CursorUtil.getColumnIndexOrThrow(_cursor, "longitude");
          final int _cursorIndexOfLastSeen = CursorUtil.getColumnIndexOrThrow(_cursor, "lastSeen");
          final int _cursorIndexOfBatteryMilliVolts = CursorUtil.getColumnIndexOrThrow(_cursor, "batteryMilliVolts");
          final int _cursorIndexOfIsRepeater = CursorUtil.getColumnIndexOrThrow(_cursor, "isRepeater");
          final int _cursorIndexOfIsRoomServer = CursorUtil.getColumnIndexOrThrow(_cursor, "isRoomServer");
          final int _cursorIndexOfIsDirect = CursorUtil.getColumnIndexOrThrow(_cursor, "isDirect");
          final int _cursorIndexOfHopCount = CursorUtil.getColumnIndexOrThrow(_cursor, "hopCount");
          final List<NodeEntity> _result = new ArrayList<NodeEntity>(_cursor.getCount());
          while (_cursor.moveToNext()) {
            final NodeEntity _item;
            final byte[] _tmpPublicKey;
            _tmpPublicKey = _cursor.getBlob(_cursorIndexOfPublicKey);
            final byte _tmpHash;
            _tmpHash = (byte) (_cursor.getShort(_cursorIndexOfHash));
            final String _tmpName;
            if (_cursor.isNull(_cursorIndexOfName)) {
              _tmpName = null;
            } else {
              _tmpName = _cursor.getString(_cursorIndexOfName);
            }
            final Double _tmpLatitude;
            if (_cursor.isNull(_cursorIndexOfLatitude)) {
              _tmpLatitude = null;
            } else {
              _tmpLatitude = _cursor.getDouble(_cursorIndexOfLatitude);
            }
            final Double _tmpLongitude;
            if (_cursor.isNull(_cursorIndexOfLongitude)) {
              _tmpLongitude = null;
            } else {
              _tmpLongitude = _cursor.getDouble(_cursorIndexOfLongitude);
            }
            final long _tmpLastSeen;
            _tmpLastSeen = _cursor.getLong(_cursorIndexOfLastSeen);
            final Integer _tmpBatteryMilliVolts;
            if (_cursor.isNull(_cursorIndexOfBatteryMilliVolts)) {
              _tmpBatteryMilliVolts = null;
            } else {
              _tmpBatteryMilliVolts = _cursor.getInt(_cursorIndexOfBatteryMilliVolts);
            }
            final boolean _tmpIsRepeater;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsRepeater);
            _tmpIsRepeater = _tmp != 0;
            final boolean _tmpIsRoomServer;
            final int _tmp_1;
            _tmp_1 = _cursor.getInt(_cursorIndexOfIsRoomServer);
            _tmpIsRoomServer = _tmp_1 != 0;
            final boolean _tmpIsDirect;
            final int _tmp_2;
            _tmp_2 = _cursor.getInt(_cursorIndexOfIsDirect);
            _tmpIsDirect = _tmp_2 != 0;
            final int _tmpHopCount;
            _tmpHopCount = _cursor.getInt(_cursorIndexOfHopCount);
            _item = new NodeEntity(_tmpPublicKey,_tmpHash,_tmpName,_tmpLatitude,_tmpLongitude,_tmpLastSeen,_tmpBatteryMilliVolts,_tmpIsRepeater,_tmpIsRoomServer,_tmpIsDirect,_tmpHopCount);
            _result.add(_item);
          }
          return _result;
        } finally {
          _cursor.close();
        }
      }

      @Override
      protected void finalize() {
        _statement.release();
      }
    });
  }

  @Override
  public Object getAllNodesDirect(final Continuation<? super List<NodeEntity>> $completion) {
    final String _sql = "SELECT * FROM nodes ORDER BY lastSeen DESC";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 0);
    final CancellationSignal _cancellationSignal = DBUtil.createCancellationSignal();
    return CoroutinesRoom.execute(__db, false, _cancellationSignal, new Callable<List<NodeEntity>>() {
      @Override
      @NonNull
      public List<NodeEntity> call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfPublicKey = CursorUtil.getColumnIndexOrThrow(_cursor, "publicKey");
          final int _cursorIndexOfHash = CursorUtil.getColumnIndexOrThrow(_cursor, "hash");
          final int _cursorIndexOfName = CursorUtil.getColumnIndexOrThrow(_cursor, "name");
          final int _cursorIndexOfLatitude = CursorUtil.getColumnIndexOrThrow(_cursor, "latitude");
          final int _cursorIndexOfLongitude = CursorUtil.getColumnIndexOrThrow(_cursor, "longitude");
          final int _cursorIndexOfLastSeen = CursorUtil.getColumnIndexOrThrow(_cursor, "lastSeen");
          final int _cursorIndexOfBatteryMilliVolts = CursorUtil.getColumnIndexOrThrow(_cursor, "batteryMilliVolts");
          final int _cursorIndexOfIsRepeater = CursorUtil.getColumnIndexOrThrow(_cursor, "isRepeater");
          final int _cursorIndexOfIsRoomServer = CursorUtil.getColumnIndexOrThrow(_cursor, "isRoomServer");
          final int _cursorIndexOfIsDirect = CursorUtil.getColumnIndexOrThrow(_cursor, "isDirect");
          final int _cursorIndexOfHopCount = CursorUtil.getColumnIndexOrThrow(_cursor, "hopCount");
          final List<NodeEntity> _result = new ArrayList<NodeEntity>(_cursor.getCount());
          while (_cursor.moveToNext()) {
            final NodeEntity _item;
            final byte[] _tmpPublicKey;
            _tmpPublicKey = _cursor.getBlob(_cursorIndexOfPublicKey);
            final byte _tmpHash;
            _tmpHash = (byte) (_cursor.getShort(_cursorIndexOfHash));
            final String _tmpName;
            if (_cursor.isNull(_cursorIndexOfName)) {
              _tmpName = null;
            } else {
              _tmpName = _cursor.getString(_cursorIndexOfName);
            }
            final Double _tmpLatitude;
            if (_cursor.isNull(_cursorIndexOfLatitude)) {
              _tmpLatitude = null;
            } else {
              _tmpLatitude = _cursor.getDouble(_cursorIndexOfLatitude);
            }
            final Double _tmpLongitude;
            if (_cursor.isNull(_cursorIndexOfLongitude)) {
              _tmpLongitude = null;
            } else {
              _tmpLongitude = _cursor.getDouble(_cursorIndexOfLongitude);
            }
            final long _tmpLastSeen;
            _tmpLastSeen = _cursor.getLong(_cursorIndexOfLastSeen);
            final Integer _tmpBatteryMilliVolts;
            if (_cursor.isNull(_cursorIndexOfBatteryMilliVolts)) {
              _tmpBatteryMilliVolts = null;
            } else {
              _tmpBatteryMilliVolts = _cursor.getInt(_cursorIndexOfBatteryMilliVolts);
            }
            final boolean _tmpIsRepeater;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsRepeater);
            _tmpIsRepeater = _tmp != 0;
            final boolean _tmpIsRoomServer;
            final int _tmp_1;
            _tmp_1 = _cursor.getInt(_cursorIndexOfIsRoomServer);
            _tmpIsRoomServer = _tmp_1 != 0;
            final boolean _tmpIsDirect;
            final int _tmp_2;
            _tmp_2 = _cursor.getInt(_cursorIndexOfIsDirect);
            _tmpIsDirect = _tmp_2 != 0;
            final int _tmpHopCount;
            _tmpHopCount = _cursor.getInt(_cursorIndexOfHopCount);
            _item = new NodeEntity(_tmpPublicKey,_tmpHash,_tmpName,_tmpLatitude,_tmpLongitude,_tmpLastSeen,_tmpBatteryMilliVolts,_tmpIsRepeater,_tmpIsRoomServer,_tmpIsDirect,_tmpHopCount);
            _result.add(_item);
          }
          return _result;
        } finally {
          _cursor.close();
          _statement.release();
        }
      }
    }, $completion);
  }

  @Override
  public Object getNodeByPublicKey(final byte[] publicKey,
      final Continuation<? super NodeEntity> $completion) {
    final String _sql = "SELECT * FROM nodes WHERE publicKey = ?";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 1);
    int _argIndex = 1;
    _statement.bindBlob(_argIndex, publicKey);
    final CancellationSignal _cancellationSignal = DBUtil.createCancellationSignal();
    return CoroutinesRoom.execute(__db, false, _cancellationSignal, new Callable<NodeEntity>() {
      @Override
      @Nullable
      public NodeEntity call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfPublicKey = CursorUtil.getColumnIndexOrThrow(_cursor, "publicKey");
          final int _cursorIndexOfHash = CursorUtil.getColumnIndexOrThrow(_cursor, "hash");
          final int _cursorIndexOfName = CursorUtil.getColumnIndexOrThrow(_cursor, "name");
          final int _cursorIndexOfLatitude = CursorUtil.getColumnIndexOrThrow(_cursor, "latitude");
          final int _cursorIndexOfLongitude = CursorUtil.getColumnIndexOrThrow(_cursor, "longitude");
          final int _cursorIndexOfLastSeen = CursorUtil.getColumnIndexOrThrow(_cursor, "lastSeen");
          final int _cursorIndexOfBatteryMilliVolts = CursorUtil.getColumnIndexOrThrow(_cursor, "batteryMilliVolts");
          final int _cursorIndexOfIsRepeater = CursorUtil.getColumnIndexOrThrow(_cursor, "isRepeater");
          final int _cursorIndexOfIsRoomServer = CursorUtil.getColumnIndexOrThrow(_cursor, "isRoomServer");
          final int _cursorIndexOfIsDirect = CursorUtil.getColumnIndexOrThrow(_cursor, "isDirect");
          final int _cursorIndexOfHopCount = CursorUtil.getColumnIndexOrThrow(_cursor, "hopCount");
          final NodeEntity _result;
          if (_cursor.moveToFirst()) {
            final byte[] _tmpPublicKey;
            _tmpPublicKey = _cursor.getBlob(_cursorIndexOfPublicKey);
            final byte _tmpHash;
            _tmpHash = (byte) (_cursor.getShort(_cursorIndexOfHash));
            final String _tmpName;
            if (_cursor.isNull(_cursorIndexOfName)) {
              _tmpName = null;
            } else {
              _tmpName = _cursor.getString(_cursorIndexOfName);
            }
            final Double _tmpLatitude;
            if (_cursor.isNull(_cursorIndexOfLatitude)) {
              _tmpLatitude = null;
            } else {
              _tmpLatitude = _cursor.getDouble(_cursorIndexOfLatitude);
            }
            final Double _tmpLongitude;
            if (_cursor.isNull(_cursorIndexOfLongitude)) {
              _tmpLongitude = null;
            } else {
              _tmpLongitude = _cursor.getDouble(_cursorIndexOfLongitude);
            }
            final long _tmpLastSeen;
            _tmpLastSeen = _cursor.getLong(_cursorIndexOfLastSeen);
            final Integer _tmpBatteryMilliVolts;
            if (_cursor.isNull(_cursorIndexOfBatteryMilliVolts)) {
              _tmpBatteryMilliVolts = null;
            } else {
              _tmpBatteryMilliVolts = _cursor.getInt(_cursorIndexOfBatteryMilliVolts);
            }
            final boolean _tmpIsRepeater;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsRepeater);
            _tmpIsRepeater = _tmp != 0;
            final boolean _tmpIsRoomServer;
            final int _tmp_1;
            _tmp_1 = _cursor.getInt(_cursorIndexOfIsRoomServer);
            _tmpIsRoomServer = _tmp_1 != 0;
            final boolean _tmpIsDirect;
            final int _tmp_2;
            _tmp_2 = _cursor.getInt(_cursorIndexOfIsDirect);
            _tmpIsDirect = _tmp_2 != 0;
            final int _tmpHopCount;
            _tmpHopCount = _cursor.getInt(_cursorIndexOfHopCount);
            _result = new NodeEntity(_tmpPublicKey,_tmpHash,_tmpName,_tmpLatitude,_tmpLongitude,_tmpLastSeen,_tmpBatteryMilliVolts,_tmpIsRepeater,_tmpIsRoomServer,_tmpIsDirect,_tmpHopCount);
          } else {
            _result = null;
          }
          return _result;
        } finally {
          _cursor.close();
          _statement.release();
        }
      }
    }, $completion);
  }

  @Override
  public Object getNodeByHash(final byte hash, final Continuation<? super NodeEntity> $completion) {
    final String _sql = "SELECT * FROM nodes WHERE hash = ?";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 1);
    int _argIndex = 1;
    _statement.bindLong(_argIndex, hash);
    final CancellationSignal _cancellationSignal = DBUtil.createCancellationSignal();
    return CoroutinesRoom.execute(__db, false, _cancellationSignal, new Callable<NodeEntity>() {
      @Override
      @Nullable
      public NodeEntity call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfPublicKey = CursorUtil.getColumnIndexOrThrow(_cursor, "publicKey");
          final int _cursorIndexOfHash = CursorUtil.getColumnIndexOrThrow(_cursor, "hash");
          final int _cursorIndexOfName = CursorUtil.getColumnIndexOrThrow(_cursor, "name");
          final int _cursorIndexOfLatitude = CursorUtil.getColumnIndexOrThrow(_cursor, "latitude");
          final int _cursorIndexOfLongitude = CursorUtil.getColumnIndexOrThrow(_cursor, "longitude");
          final int _cursorIndexOfLastSeen = CursorUtil.getColumnIndexOrThrow(_cursor, "lastSeen");
          final int _cursorIndexOfBatteryMilliVolts = CursorUtil.getColumnIndexOrThrow(_cursor, "batteryMilliVolts");
          final int _cursorIndexOfIsRepeater = CursorUtil.getColumnIndexOrThrow(_cursor, "isRepeater");
          final int _cursorIndexOfIsRoomServer = CursorUtil.getColumnIndexOrThrow(_cursor, "isRoomServer");
          final int _cursorIndexOfIsDirect = CursorUtil.getColumnIndexOrThrow(_cursor, "isDirect");
          final int _cursorIndexOfHopCount = CursorUtil.getColumnIndexOrThrow(_cursor, "hopCount");
          final NodeEntity _result;
          if (_cursor.moveToFirst()) {
            final byte[] _tmpPublicKey;
            _tmpPublicKey = _cursor.getBlob(_cursorIndexOfPublicKey);
            final byte _tmpHash;
            _tmpHash = (byte) (_cursor.getShort(_cursorIndexOfHash));
            final String _tmpName;
            if (_cursor.isNull(_cursorIndexOfName)) {
              _tmpName = null;
            } else {
              _tmpName = _cursor.getString(_cursorIndexOfName);
            }
            final Double _tmpLatitude;
            if (_cursor.isNull(_cursorIndexOfLatitude)) {
              _tmpLatitude = null;
            } else {
              _tmpLatitude = _cursor.getDouble(_cursorIndexOfLatitude);
            }
            final Double _tmpLongitude;
            if (_cursor.isNull(_cursorIndexOfLongitude)) {
              _tmpLongitude = null;
            } else {
              _tmpLongitude = _cursor.getDouble(_cursorIndexOfLongitude);
            }
            final long _tmpLastSeen;
            _tmpLastSeen = _cursor.getLong(_cursorIndexOfLastSeen);
            final Integer _tmpBatteryMilliVolts;
            if (_cursor.isNull(_cursorIndexOfBatteryMilliVolts)) {
              _tmpBatteryMilliVolts = null;
            } else {
              _tmpBatteryMilliVolts = _cursor.getInt(_cursorIndexOfBatteryMilliVolts);
            }
            final boolean _tmpIsRepeater;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsRepeater);
            _tmpIsRepeater = _tmp != 0;
            final boolean _tmpIsRoomServer;
            final int _tmp_1;
            _tmp_1 = _cursor.getInt(_cursorIndexOfIsRoomServer);
            _tmpIsRoomServer = _tmp_1 != 0;
            final boolean _tmpIsDirect;
            final int _tmp_2;
            _tmp_2 = _cursor.getInt(_cursorIndexOfIsDirect);
            _tmpIsDirect = _tmp_2 != 0;
            final int _tmpHopCount;
            _tmpHopCount = _cursor.getInt(_cursorIndexOfHopCount);
            _result = new NodeEntity(_tmpPublicKey,_tmpHash,_tmpName,_tmpLatitude,_tmpLongitude,_tmpLastSeen,_tmpBatteryMilliVolts,_tmpIsRepeater,_tmpIsRoomServer,_tmpIsDirect,_tmpHopCount);
          } else {
            _result = null;
          }
          return _result;
        } finally {
          _cursor.close();
          _statement.release();
        }
      }
    }, $completion);
  }

  @NonNull
  public static List<Class<?>> getRequiredConverters() {
    return Collections.emptyList();
  }
}
