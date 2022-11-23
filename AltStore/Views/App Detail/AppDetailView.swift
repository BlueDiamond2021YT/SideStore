//
//  AppDetailView.swift
//  SideStore
//
//  Created by Fabian Thies on 18.11.22.
//  Copyright © 2022 Fabian Thies. All rights reserved.
//

import SwiftUI
import GridStack
import AltStoreCore

struct AppDetailView: View {
    
    let storeApp: StoreApp

    var dateFormatter: DateFormatter = {
        let dateFormatter = DateFormatter()
        dateFormatter.dateStyle = .medium
        dateFormatter.timeStyle = .none
        return dateFormatter
    }()

    var byteCountFormatter: ByteCountFormatter = {
        let formatter = ByteCountFormatter()
        return formatter
    }()
    
    @State var scrollOffset: CGFloat = .zero
    let maxContentCornerRadius: CGFloat = 24
    let headerViewHeight: CGFloat = 140
    let permissionColumns = 4
    
    var isHeaderViewVisible: Bool {
        scrollOffset < headerViewHeight + 12
    }
    var contentCornerRadius: CGFloat {
        max(CGFloat.zero, min(maxContentCornerRadius, maxContentCornerRadius * (1 - self.scrollOffset / self.headerViewHeight)))
    }
    
    var body: some View {
        ObservableScrollView(scrollOffset: self.$scrollOffset) { proxy in
            LazyVStack {
                headerView
                    .frame(height: headerViewHeight)
                
                contentView
            }
        }
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItemGroup(placement: .navigationBarTrailing) {
                AppPillButton(app: storeApp)
                    .disabled(isHeaderViewVisible)
                    .offset(y: isHeaderViewVisible ? 12 : 0)
                    .opacity(isHeaderViewVisible ? 0 : 1)
                    .animation(.easeInOut(duration: 0.2), value: isHeaderViewVisible)
            }
            
            ToolbarItemGroup(placement: .principal) {
                HStack {
                    Spacer()
                    AppIconView(iconUrl: storeApp.iconURL, size: 24)
                    Text(storeApp.name)
                        .bold()
                    Spacer()
                }
                .offset(y: isHeaderViewVisible ? 12 : 0)
                .opacity(isHeaderViewVisible ? 0 : 1)
                .animation(.easeInOut(duration: 0.2), value: isHeaderViewVisible)
            }
        }
    }
    
    
    var headerView: some View {
        ZStack(alignment: .center) {
            GeometryReader { proxy in
                AppIconView(iconUrl: storeApp.iconURL, size: proxy.frame(in: .global).width)
                    .blur(radius: 20)
            }
            
            AppRowView(app: storeApp)
                .padding(.horizontal)
        }
    }
    
    var contentView: some View {
        VStack(alignment: .leading) {
            if let subtitle = storeApp.subtitle {
                Text(subtitle)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: .infinity)
                    .padding()
            }
            
            if !storeApp.screenshotURLs.isEmpty {
                screenshotsView
            }
            
            Text(storeApp.localizedDescription)
                .lineLimit(6)
                .padding()
            
            currentVersionView
            
            permissionsView
            
            // TODO: Add review view
            // Only let users rate the app if it is currently installed!
        }
        .background(
            RoundedRectangle(cornerRadius: contentCornerRadius)
                .foregroundColor(Color(UIColor.systemBackground))
                .shadow(radius: isHeaderViewVisible ? 12 : 0)
        )
    }
    
    var screenshotsView: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack {
                ForEach(storeApp.screenshotURLs) { url in
                    if #available(iOS 15.0, *) {
                        AsyncImage(url: url) { image in
                            image.resizable()
                        } placeholder: {
                            Rectangle()
                                .foregroundColor(.secondary)
                        }
                        .aspectRatio(9/16, contentMode: .fit)
                        .cornerRadius(8)
                    }
                }
            }
            .padding(.horizontal)
        }
        .frame(height: 400)
        .shadow(radius: 12)
    }
    
    var currentVersionView: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .bottom) {
                VStack(alignment: .leading) {
                    Text("What's New")
                        .bold()
                        .font(.title3)
                    
                    Text("Version \(storeApp.version)")
                        .font(.callout)
                        .foregroundColor(.secondary)
                }
                
                Spacer()
                
                VStack(alignment: .trailing) {
                    Text(dateFormatter.string(from: storeApp.versionDate))
                    Text(byteCountFormatter.string(fromByteCount: Int64(storeApp.size)))
                }
                .font(.callout)
                .foregroundColor(.secondary)
            }
            
            if let versionDescription = storeApp.versionDescription {
                Text(versionDescription)
                    .lineLimit(5)
            } else {
                Text("No version information")
                    .foregroundColor(.secondary)
            }
        }
        .padding()
    }
    
    var permissionsView: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Permissions")
                .bold()
                .font(.title3)
            
            if storeApp.permissions.isEmpty {
                Text("The app requires no permissions.")
                    .font(.callout)
                    .foregroundColor(.secondary)
            } else {
                GridStack(minCellWidth: 40, spacing: 8, numItems: storeApp.permissions.count, alignment: .leading) { index, cellWidth in
                    let permission = storeApp.permissions[index]
                    
                    VStack {
                        Image(systemName: "photo.on.rectangle")
                            .padding()
                            .background(Circle().foregroundColor(Color(UIColor.secondarySystemBackground)))
                        Text(permission.type.localizedShortName ?? "")
                    }
                    .frame(width: cellWidth, height: cellWidth * 1.2)
                    .background(Color.red)
                }
            }
            
            Spacer()
        }
        .padding()
    }
}

//struct AppDetailView_Previews: PreviewProvider {
//    static var previews: some View {
//        AppDetailView()
//    }
//}