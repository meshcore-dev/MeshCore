package com.meshcore.team.data.database;

import android.database.Cursor;
import android.os.CancellationSignal;
import androidx.annotation.NonNull;
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
public final class AckRecordDao_Impl implements AckRecordDao {
  private final RoomDatabase __db;

  private final EntityInsertionAdapter<AckRecordEntity> __insertionAdapterOfAckRecordEntity;

  private final EntityDeletionOrUpdateAdapter<AckRecordEntity> __deletionAdapterOfAckRecordEntity;

  private final SharedSQLiteStatement __preparedStmtOfDeleteAcksByMessageId;

  private final SharedSQLiteStatement __preparedStmtOfDeleteAllAcks;

  public AckRecordDao_Impl(@NonNull final RoomDatabase __db) {
    this.__db = __db;
    this.__insertionAdapterOfAckRecordEntity = new EntityInsertionAdapter<AckRecordEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "INSERT OR REPLACE INTO `ack_records` (`id`,`messageId`,`ackChecksum`,`devicePublicKey`,`roundTripTimeMs`,`isDirect`,`receivedAt`) VALUES (nullif(?, 0),?,?,?,?,?,?)";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final AckRecordEntity entity) {
        statement.bindLong(1, entity.getId());
        statement.bindString(2, entity.getMessageId());
        statement.bindBlob(3, entity.getAckChecksum());
        statement.bindBlob(4, entity.getDevicePublicKey());
        statement.bindLong(5, entity.getRoundTripTimeMs());
        final int _tmp = entity.isDirect() ? 1 : 0;
        statement.bindLong(6, _tmp);
        statement.bindLong(7, entity.getReceivedAt());
      }
    };
    this.__deletionAdapterOfAckRecordEntity = new EntityDeletionOrUpdateAdapter<AckRecordEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "DELETE FROM `ack_records` WHERE `id` = ?";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final AckRecordEntity entity) {
        statement.bindLong(1, entity.getId());
      }
    };
    this.__preparedStmtOfDeleteAcksByMessageId = new SharedSQLiteStatement(__db) {
      @Override
      @NonNull
      public String createQuery() {
        final String _query = "DELETE FROM ack_records WHERE messageId = ?";
        return _query;
      }
    };
    this.__preparedStmtOfDeleteAllAcks = new SharedSQLiteStatement(__db) {
      @Override
      @NonNull
      public String createQuery() {
        final String _query = "DELETE FROM ack_records";
        return _query;
      }
    };
  }

  @Override
  public Object insertAck(final AckRecordEntity ack, final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __insertionAdapterOfAckRecordEntity.insert(ack);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object deleteAck(final AckRecordEntity ack, final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __deletionAdapterOfAckRecordEntity.handle(ack);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object deleteAcksByMessageId(final String messageId,
      final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        final SupportSQLiteStatement _stmt = __preparedStmtOfDeleteAcksByMessageId.acquire();
        int _argIndex = 1;
        _stmt.bindString(_argIndex, messageId);
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
          __preparedStmtOfDeleteAcksByMessageId.release(_stmt);
        }
      }
    }, $completion);
  }

  @Override
  public Object deleteAllAcks(final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        final SupportSQLiteStatement _stmt = __preparedStmtOfDeleteAllAcks.acquire();
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
          __preparedStmtOfDeleteAllAcks.release(_stmt);
        }
      }
    }, $completion);
  }

  @Override
  public Flow<List<AckRecordEntity>> getAcksByMessageId(final String messageId) {
    final String _sql = "SELECT * FROM ack_records WHERE messageId = ?";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 1);
    int _argIndex = 1;
    _statement.bindString(_argIndex, messageId);
    return CoroutinesRoom.createFlow(__db, false, new String[] {"ack_records"}, new Callable<List<AckRecordEntity>>() {
      @Override
      @NonNull
      public List<AckRecordEntity> call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfId = CursorUtil.getColumnIndexOrThrow(_cursor, "id");
          final int _cursorIndexOfMessageId = CursorUtil.getColumnIndexOrThrow(_cursor, "messageId");
          final int _cursorIndexOfAckChecksum = CursorUtil.getColumnIndexOrThrow(_cursor, "ackChecksum");
          final int _cursorIndexOfDevicePublicKey = CursorUtil.getColumnIndexOrThrow(_cursor, "devicePublicKey");
          final int _cursorIndexOfRoundTripTimeMs = CursorUtil.getColumnIndexOrThrow(_cursor, "roundTripTimeMs");
          final int _cursorIndexOfIsDirect = CursorUtil.getColumnIndexOrThrow(_cursor, "isDirect");
          final int _cursorIndexOfReceivedAt = CursorUtil.getColumnIndexOrThrow(_cursor, "receivedAt");
          final List<AckRecordEntity> _result = new ArrayList<AckRecordEntity>(_cursor.getCount());
          while (_cursor.moveToNext()) {
            final AckRecordEntity _item;
            final long _tmpId;
            _tmpId = _cursor.getLong(_cursorIndexOfId);
            final String _tmpMessageId;
            _tmpMessageId = _cursor.getString(_cursorIndexOfMessageId);
            final byte[] _tmpAckChecksum;
            _tmpAckChecksum = _cursor.getBlob(_cursorIndexOfAckChecksum);
            final byte[] _tmpDevicePublicKey;
            _tmpDevicePublicKey = _cursor.getBlob(_cursorIndexOfDevicePublicKey);
            final int _tmpRoundTripTimeMs;
            _tmpRoundTripTimeMs = _cursor.getInt(_cursorIndexOfRoundTripTimeMs);
            final boolean _tmpIsDirect;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsDirect);
            _tmpIsDirect = _tmp != 0;
            final long _tmpReceivedAt;
            _tmpReceivedAt = _cursor.getLong(_cursorIndexOfReceivedAt);
            _item = new AckRecordEntity(_tmpId,_tmpMessageId,_tmpAckChecksum,_tmpDevicePublicKey,_tmpRoundTripTimeMs,_tmpIsDirect,_tmpReceivedAt);
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
  public Object getAcksByChecksum(final byte[] checksum,
      final Continuation<? super List<AckRecordEntity>> $completion) {
    final String _sql = "SELECT * FROM ack_records WHERE ackChecksum = ?";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 1);
    int _argIndex = 1;
    _statement.bindBlob(_argIndex, checksum);
    final CancellationSignal _cancellationSignal = DBUtil.createCancellationSignal();
    return CoroutinesRoom.execute(__db, false, _cancellationSignal, new Callable<List<AckRecordEntity>>() {
      @Override
      @NonNull
      public List<AckRecordEntity> call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfId = CursorUtil.getColumnIndexOrThrow(_cursor, "id");
          final int _cursorIndexOfMessageId = CursorUtil.getColumnIndexOrThrow(_cursor, "messageId");
          final int _cursorIndexOfAckChecksum = CursorUtil.getColumnIndexOrThrow(_cursor, "ackChecksum");
          final int _cursorIndexOfDevicePublicKey = CursorUtil.getColumnIndexOrThrow(_cursor, "devicePublicKey");
          final int _cursorIndexOfRoundTripTimeMs = CursorUtil.getColumnIndexOrThrow(_cursor, "roundTripTimeMs");
          final int _cursorIndexOfIsDirect = CursorUtil.getColumnIndexOrThrow(_cursor, "isDirect");
          final int _cursorIndexOfReceivedAt = CursorUtil.getColumnIndexOrThrow(_cursor, "receivedAt");
          final List<AckRecordEntity> _result = new ArrayList<AckRecordEntity>(_cursor.getCount());
          while (_cursor.moveToNext()) {
            final AckRecordEntity _item;
            final long _tmpId;
            _tmpId = _cursor.getLong(_cursorIndexOfId);
            final String _tmpMessageId;
            _tmpMessageId = _cursor.getString(_cursorIndexOfMessageId);
            final byte[] _tmpAckChecksum;
            _tmpAckChecksum = _cursor.getBlob(_cursorIndexOfAckChecksum);
            final byte[] _tmpDevicePublicKey;
            _tmpDevicePublicKey = _cursor.getBlob(_cursorIndexOfDevicePublicKey);
            final int _tmpRoundTripTimeMs;
            _tmpRoundTripTimeMs = _cursor.getInt(_cursorIndexOfRoundTripTimeMs);
            final boolean _tmpIsDirect;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsDirect);
            _tmpIsDirect = _tmp != 0;
            final long _tmpReceivedAt;
            _tmpReceivedAt = _cursor.getLong(_cursorIndexOfReceivedAt);
            _item = new AckRecordEntity(_tmpId,_tmpMessageId,_tmpAckChecksum,_tmpDevicePublicKey,_tmpRoundTripTimeMs,_tmpIsDirect,_tmpReceivedAt);
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
  public Object getAckCountForMessage(final String messageId,
      final Continuation<? super Integer> $completion) {
    final String _sql = "SELECT COUNT(*) FROM ack_records WHERE messageId = ?";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 1);
    int _argIndex = 1;
    _statement.bindString(_argIndex, messageId);
    final CancellationSignal _cancellationSignal = DBUtil.createCancellationSignal();
    return CoroutinesRoom.execute(__db, false, _cancellationSignal, new Callable<Integer>() {
      @Override
      @NonNull
      public Integer call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final Integer _result;
          if (_cursor.moveToFirst()) {
            final int _tmp;
            _tmp = _cursor.getInt(0);
            _result = _tmp;
          } else {
            _result = 0;
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
  public Object getDirectAckCountForMessage(final String messageId,
      final Continuation<? super Integer> $completion) {
    final String _sql = "SELECT COUNT(*) FROM ack_records WHERE messageId = ? AND isDirect = 1";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 1);
    int _argIndex = 1;
    _statement.bindString(_argIndex, messageId);
    final CancellationSignal _cancellationSignal = DBUtil.createCancellationSignal();
    return CoroutinesRoom.execute(__db, false, _cancellationSignal, new Callable<Integer>() {
      @Override
      @NonNull
      public Integer call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final Integer _result;
          if (_cursor.moveToFirst()) {
            final int _tmp;
            _tmp = _cursor.getInt(0);
            _result = _tmp;
          } else {
            _result = 0;
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
