# Running

1. Download `MotionView` from the [Official GitHub Link](https://github.com/lewispinstein-hue/MotionView/tree/main/Releases/MacOS)
2. Double-click the downloaded file
3. Drag MotionView into `applications` when prompted
4.

```zsh
xattr -dr com.apple.quarantine /Applications/MotionView.app
```

If `/Applications/MotionView.app` is not found, navigate to your Applications folder, find MotionView, and copy it as a path name. Then, run:

```zsh
xattr -dr com.apple.quarantine {paste-what-you-copied}
```

This should allow for you to launch MotionView.

## Fixing launch error
**Q:** When I launched MotionView, a popup appeared saying that `MotionView is damaged and could not be run`. <br>
**A:** This is likely *not* because the app was damaged in downloading, and can be fixed by following the steps above. 

## Why this is needed
Unfortunately, I am current not enrolled in the Apple Development Program. Because of this, I am unable to get MotionView to work out-of-the-box and get it verified with Apple. Right now, this is currently the best way to run MotionView on your Macbook.