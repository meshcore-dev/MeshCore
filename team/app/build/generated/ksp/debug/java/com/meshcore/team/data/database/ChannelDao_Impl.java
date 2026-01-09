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
import java.lang.Exception;
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
public final class ChannelDao_Impl implements ChannelDao {
  private final RoomDatabase __db;

  private final EntityInsertionAdapter<ChannelEntity> __insertionAdapterOfChannelEntity;

  private final EntityDeletionOrUpdateAdapter<ChannelEntity> __deletionAdapterOfChannelEntity;

  private final EntityDeletionOrUpdateAdapter<ChannelEntity> __updateAdapterOfChannelEntity;

  private final SharedSQLiteStatement __preparedStmtOfUpdateChannelLocationSharing;

  public ChannelDao_Impl(@NonNull final RoomDatabase __db) {
    this.__db = __db;
    this.__insertionAdapterOfChannelEntity = new EntityInsertionAdapter<ChannelEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "INSERT OR REPLACE INTO `channels` (`hash`,`name`,`sharedKey`,`isPublic`,`shareLocation`,`createdAt`) VALUES (?,?,?,?,?,?)";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final ChannelEntity entity) {
        statement.bindLong(1, entity.getHash());
        statement.bindString(2, entity.getName());
        statement.bindBlob(3, entity.getSharedKey());
        final int _tmp = entity.isPublic() ? 1 : 0;
        statement.bindLong(4, _tmp);
        final int _tmp_1 = entity.getShareLocation() ? 1 : 0;
        statement.bindLong(5, _tmp_1);
        statement.bindLong(6, entity.getCreatedAt());
      }
    };
    this.__deletionAdapterOfChannelEntity = new EntityDeletionOrUpdateAdapter<ChannelEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "DELETE FROM `channels` WHERE `hash` = ?";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final ChannelEntity entity) {
        statement.bindLong(1, entity.getHash());
      }
    };
    this.__updateAdapterOfChannelEntity = new EntityDeletionOrUpdateAdapter<ChannelEntity>(__db) {
      @Override
      @NonNull
      protected String createQuery() {
        return "UPDATE OR ABORT `channels` SET `hash` = ?,`name` = ?,`sharedKey` = ?,`isPublic` = ?,`shareLocation` = ?,`createdAt` = ? WHERE `hash` = ?";
      }

      @Override
      protected void bind(@NonNull final SupportSQLiteStatement statement,
          @NonNull final ChannelEntity entity) {
        statement.bindLong(1, entity.getHash());
        statement.bindString(2, entity.getName());
        statement.bindBlob(3, entity.getSharedKey());
        final int _tmp = entity.isPublic() ? 1 : 0;
        statement.bindLong(4, _tmp);
        final int _tmp_1 = entity.getShareLocation() ? 1 : 0;
        statement.bindLong(5, _tmp_1);
        statement.bindLong(6, entity.getCreatedAt());
        statement.bindLong(7, entity.getHash());
      }
    };
    this.__preparedStmtOfUpdateChannelLocationSharing = new SharedSQLiteStatement(__db) {
      @Override
      @NonNull
      public String createQuery() {
        final String _query = "UPDATE channels SET shareLocation = ? WHERE hash = ?";
        return _query;
      }
    };
  }

  @Override
  public Object insertChannel(final ChannelEntity channel,
      final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __insertionAdapterOfChannelEntity.insert(channel);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object deleteChannel(final ChannelEntity channel,
      final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __deletionAdapterOfChannelEntity.handle(channel);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object updateChannel(final ChannelEntity channel,
      final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        __db.beginTransaction();
        try {
          __updateAdapterOfChannelEntity.handle(channel);
          __db.setTransactionSuccessful();
          return Unit.INSTANCE;
        } finally {
          __db.endTransaction();
        }
      }
    }, $completion);
  }

  @Override
  public Object updateChannelLocationSharing(final byte hash, final boolean shareLocation,
      final Continuation<? super Unit> $completion) {
    return CoroutinesRoom.execute(__db, true, new Callable<Unit>() {
      @Override
      @NonNull
      public Unit call() throws Exception {
        final SupportSQLiteStatement _stmt = __preparedStmtOfUpdateChannelLocationSharing.acquire();
        int _argIndex = 1;
        final int _tmp = shareLocation ? 1 : 0;
        _stmt.bindLong(_argIndex, _tmp);
        _argIndex = 2;
        _stmt.bindLong(_argIndex, hash);
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
          __preparedStmtOfUpdateChannelLocationSharing.release(_stmt);
        }
      }
    }, $completion);
  }

  @Override
  public Flow<List<ChannelEntity>> getAllChannels() {
    final String _sql = "SELECT * FROM channels ORDER BY createdAt DESC";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 0);
    return CoroutinesRoom.createFlow(__db, false, new String[] {"channels"}, new Callable<List<ChannelEntity>>() {
      @Override
      @NonNull
      public List<ChannelEntity> call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfHash = CursorUtil.getColumnIndexOrThrow(_cursor, "hash");
          final int _cursorIndexOfName = CursorUtil.getColumnIndexOrThrow(_cursor, "name");
          final int _cursorIndexOfSharedKey = CursorUtil.getColumnIndexOrThrow(_cursor, "sharedKey");
          final int _cursorIndexOfIsPublic = CursorUtil.getColumnIndexOrThrow(_cursor, "isPublic");
          final int _cursorIndexOfShareLocation = CursorUtil.getColumnIndexOrThrow(_cursor, "shareLocation");
          final int _cursorIndexOfCreatedAt = CursorUtil.getColumnIndexOrThrow(_cursor, "createdAt");
          final List<ChannelEntity> _result = new ArrayList<ChannelEntity>(_cursor.getCount());
          while (_cursor.moveToNext()) {
            final ChannelEntity _item;
            final byte _tmpHash;
            _tmpHash = (byte) (_cursor.getShort(_cursorIndexOfHash));
            final String _tmpName;
            _tmpName = _cursor.getString(_cursorIndexOfName);
            final byte[] _tmpSharedKey;
            _tmpSharedKey = _cursor.getBlob(_cursorIndexOfSharedKey);
            final boolean _tmpIsPublic;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsPublic);
            _tmpIsPublic = _tmp != 0;
            final boolean _tmpShareLocation;
            final int _tmp_1;
            _tmp_1 = _cursor.getInt(_cursorIndexOfShareLocation);
            _tmpShareLocation = _tmp_1 != 0;
            final long _tmpCreatedAt;
            _tmpCreatedAt = _cursor.getLong(_cursorIndexOfCreatedAt);
            _item = new ChannelEntity(_tmpHash,_tmpName,_tmpSharedKey,_tmpIsPublic,_tmpShareLocation,_tmpCreatedAt);
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
  public Object getChannelByHash(final byte hash,
      final Continuation<? super ChannelEntity> $completion) {
    final String _sql = "SELECT * FROM channels WHERE hash = ?";
    final RoomSQLiteQuery _statement = RoomSQLiteQuery.acquire(_sql, 1);
    int _argIndex = 1;
    _statement.bindLong(_argIndex, hash);
    final CancellationSignal _cancellationSignal = DBUtil.createCancellationSignal();
    return CoroutinesRoom.execute(__db, false, _cancellationSignal, new Callable<ChannelEntity>() {
      @Override
      @Nullable
      public ChannelEntity call() throws Exception {
        final Cursor _cursor = DBUtil.query(__db, _statement, false, null);
        try {
          final int _cursorIndexOfHash = CursorUtil.getColumnIndexOrThrow(_cursor, "hash");
          final int _cursorIndexOfName = CursorUtil.getColumnIndexOrThrow(_cursor, "name");
          final int _cursorIndexOfSharedKey = CursorUtil.getColumnIndexOrThrow(_cursor, "sharedKey");
          final int _cursorIndexOfIsPublic = CursorUtil.getColumnIndexOrThrow(_cursor, "isPublic");
          final int _cursorIndexOfShareLocation = CursorUtil.getColumnIndexOrThrow(_cursor, "shareLocation");
          final int _cursorIndexOfCreatedAt = CursorUtil.getColumnIndexOrThrow(_cursor, "createdAt");
          final ChannelEntity _result;
          if (_cursor.moveToFirst()) {
            final byte _tmpHash;
            _tmpHash = (byte) (_cursor.getShort(_cursorIndexOfHash));
            final String _tmpName;
            _tmpName = _cursor.getString(_cursorIndexOfName);
            final byte[] _tmpSharedKey;
            _tmpSharedKey = _cursor.getBlob(_cursorIndexOfSharedKey);
            final boolean _tmpIsPublic;
            final int _tmp;
            _tmp = _cursor.getInt(_cursorIndexOfIsPublic);
            _tmpIsPublic = _tmp != 0;
            final boolean _tmpShareLocation;
            final int _tmp_1;
            _tmp_1 = _cursor.getInt(_cursorIndexOfShareLocation);
            _tmpShareLocation = _tmp_1 != 0;
            final long _tmpCreatedAt;
            _tmpCreatedAt = _cursor.getLong(_cursorIndexOfCreatedAt);
            _result = new ChannelEntity(_tmpHash,_tmpName,_tmpSharedKey,_tmpIsPublic,_tmpShareLocation,_tmpCreatedAt);
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
